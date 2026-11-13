// 查询标签：标签栏渲染、编辑器状态同步、表数据标签、运行/取消按钮。
(function () {
    'use strict';

    const U = window.SilkitUtil;

    function create(ctx) {
        const state = ctx.state;
        const query = ctx.query;
        const results = ctx.results;
        const toast = ctx.toast;

        const dom = {};
        let querySeq = 0;

        function byId(id) { return document.getElementById(id); }

        function init() {
            dom.tabbar = byId('tabbar');
            dom.tabAdd = byId('tabAdd');
            dom.editor = byId('sqlEditor');
            dom.gutter = byId('lineGutter');
            dom.title = byId('editorTitle');
            dom.sub = byId('editorSub');
            dom.run = byId('runBtn');
            dom.cancel = byId('cancelBtn');

            dom.tabbar.addEventListener('click', function (event) {
                const close = event.target.closest('.tab-close');
                if (close) {
                    event.stopPropagation();
                    const tab = close.closest('.tab');
                    if (tab) closeTab(tab.getAttribute('data-tab-id'));
                    return;
                }
                const node = event.target.closest('.tab');
                if (node) activate(node.getAttribute('data-tab-id'));
            });

            dom.tabbar.addEventListener('auxclick', function (event) {
                if (event.button !== 1) return;
                const node = event.target.closest('.tab');
                if (node) closeTab(node.getAttribute('data-tab-id'));
            });

            dom.tabAdd.addEventListener('click', function () { openQuery(); });

            dom.editor.addEventListener('input', function () {
                const tab = state.activeTab();
                if (tab) {
                    tab.sql = dom.editor.value;
                    tab.dirty = true;
                    refreshDirty(tab);
                }
                updateGutter();
            });

            dom.editor.addEventListener('scroll', function () {
                const tab = state.activeTab();
                if (tab) tab.editorScroll = dom.editor.scrollTop;
                if (dom.gutter) dom.gutter.scrollTop = dom.editor.scrollTop;
            });

            dom.editor.addEventListener('select', saveSelection);
            dom.editor.addEventListener('keyup', saveSelection);
            dom.editor.addEventListener('mouseup', saveSelection);

            dom.editor.addEventListener('keydown', function (event) {
                if (event.key !== 'Tab') return;
                event.preventDefault();
                const start = dom.editor.selectionStart;
                const end = dom.editor.selectionEnd;
                dom.editor.setRangeText('  ', start, end, 'end');
                dom.editor.dispatchEvent(new Event('input'));
            });

            if (dom.run) {
                dom.run.addEventListener('click', function () {
                    const tab = state.activeTab();
                    if (tab) query.run(tab);
                });
            }
            if (dom.cancel) {
                dom.cancel.addEventListener('click', function () {
                    const tab = state.activeTab();
                    if (tab) query.cancel(tab);
                });
            }

            if (!state.tabs.length) {
                openQuery(null, {
                    sql: '-- Silkit M1 · SQLite\n-- 打开一个 .db 文件后，Ctrl+Enter 或 F5 执行查询\n',
                    focus: false
                });
            } else {
                activate(state.activeTabId || state.tabs[0].id);
            }
        }

        // ------------------------------------------------------------ 标签渲染
        function render() {
            if (!dom.tabbar) return;
            const add = dom.tabAdd;
            U.qsa('.tab', dom.tabbar).forEach(function (node) { node.remove(); });

            state.tabs.forEach(function (tab) {
                const node = document.createElement('div');
                node.className = 'tab' + (tab.id === state.activeTabId ? ' active' : '') +
                    (tab.running ? ' is-running' : '') + (tab.dirty ? ' is-dirty' : '');
                node.setAttribute('role', 'tab');
                node.setAttribute('tabindex', tab.id === state.activeTabId ? '0' : '-1');
                node.setAttribute('aria-selected', tab.id === state.activeTabId ? 'true' : 'false');
                node.setAttribute('data-tab-id', tab.id);
                node.title = tab.title + (tab.running ? ' · 执行中' : '');

                const icon = U.el('span', 'tab-icon');
                if (window.Icons) {
                    icon.innerHTML = window.Icons.svg(tab.kind === 'table' ? 'table' : 'terminal', 14);
                }
                const label = U.el('span', 'tab-label', tab.title);
                const dot = U.el('span', 'tab-dot');
                const close = U.el('button', 'tab-close');
                close.type = 'button';
                close.title = '关闭标签（Ctrl+W）';
                close.setAttribute('aria-label', '关闭 ' + tab.title);
                if (window.Icons) close.innerHTML = window.Icons.svg('close', 11);

                node.appendChild(icon);
                node.appendChild(label);
                if (tab.running) {
                    const spin = U.el('span', 'tab-spinner');
                    spin.setAttribute('aria-label', '执行中');
                    node.appendChild(spin);
                } else {
                    node.appendChild(dot);
                }
                node.appendChild(close);
                dom.tabbar.insertBefore(node, add);
            });

            if (add) dom.tabbar.appendChild(add);
        }

        function refreshDirty(tab) {
            if (!dom.tabbar) return;
            const node = dom.tabbar.querySelector('.tab[data-tab-id="' + tab.id + '"]');
            if (node) node.classList.toggle('is-dirty', !!tab.dirty);
        }

        function refreshRunning(tab) {
            render();
            if (state.activeTab() === tab) updateChrome(tab);
        }

        // ------------------------------------------------------------ 激活与编辑
        function saveEditor() {
            const tab = state.activeTab();
            if (!tab || !dom.editor) return;
            tab.sql = dom.editor.value;
            tab.editorScroll = dom.editor.scrollTop;
            tab.selectionStart = dom.editor.selectionStart;
            tab.selectionEnd = dom.editor.selectionEnd;
        }

        function saveSelection() {
            const tab = state.activeTab();
            if (!tab || !dom.editor) return;
            tab.selectionStart = dom.editor.selectionStart;
            tab.selectionEnd = dom.editor.selectionEnd;
        }

        function activate(id) {
            const next = state.tab(id);
            if (!next) return;
            if (state.activeTabId === id) {
                updateChrome(next);
                return;
            }
            saveEditor();
            state.setActiveTab(id);
            dom.editor.value = next.sql || '';
            const scroll = Number(next.editorScroll) || 0;
            dom.editor.scrollTop = scroll;
            updateGutter();
            try {
                const start = Number(next.selectionStart);
                const end = Number(next.selectionEnd);
                if (Number.isFinite(start) && start >= 0 && start <= dom.editor.value.length) {
                    dom.editor.setSelectionRange(start, Number.isFinite(end) ? end : start);
                }
            } catch (error) { /* ignore */ }
            render();
            updateChrome(next);
            results.show(next);
        }

        function updateChrome(tab) {
            if (!tab) return;
            if (dom.title) dom.title.textContent = tab.title;
            if (dom.sub) {
                const session = tab.sessionId ? state.session(tab.sessionId) : null;
                if (!session) {
                    dom.sub.textContent = '未连接 · 打开连接后即可执行';
                    dom.sub.classList.add('is-warn');
                } else {
                    const profile = session.profile || {};
                    const db = session.currentDb || 'main';
                    dom.sub.textContent = (profile.name || '已连接') + ' · ' + db +
                        (session.serverVersion ? ' · ' + session.serverVersion : '');
                    dom.sub.classList.remove('is-warn');
                }
            }
            if (dom.run) {
                dom.run.hidden = !!tab.running;
                dom.run.setAttribute('aria-disabled', tab.running ? 'true' : 'false');
                dom.run.title = '执行当前 SQL（Ctrl+Enter / F5）';
            }
            if (dom.cancel) {
                dom.cancel.hidden = !tab.running;
            }
        }

        function updateGutter() {
            if (!dom.editor || !dom.gutter) return;
            const lines = dom.editor.value.split('\n').length;
            const numbers = [];
            for (let i = 1; i <= Math.max(1, lines); i += 1) numbers.push(i);
            dom.gutter.textContent = numbers.join('\n');
            dom.gutter.scrollTop = dom.editor.scrollTop;
        }

        // ------------------------------------------------------------ 标签创建
        function openQuery(session, options) {
            const opts = options || {};
            saveEditor();
            querySeq += 1;
            const target = session || state.activeSession();
            const tab = state.addTab({
                kind: 'query',
                title: '查询 ' + querySeq,
                sessionId: target ? target.id : null,
                sql: opts.sql != null ? opts.sql : '',
                dirty: false
            });
            state.setActiveTab(tab.id);
            render();
            dom.editor.value = tab.sql;
            dom.editor.scrollTop = 0;
            updateGutter();
            updateChrome(tab);
            results.show(tab);
            if (opts.focus !== false) dom.editor.focus();
            return tab;
        }

        function findTableTab(sessionId, ref) {
            for (let i = 0; i < state.tabs.length; i += 1) {
                const tab = state.tabs[i];
                if (tab.kind !== 'table' || tab.sessionId !== sessionId) continue;
                if (!tab.ref) continue;
                if (tab.ref.name === ref.name && (tab.ref.database || 'main') === (ref.database || 'main')) {
                    return tab;
                }
            }
            return null;
        }

        function openTable(session, ref, options) {
            const opts = options || {};
            if (!session) {
                toast('请先打开连接', 'error');
                return null;
            }
            const reference = {
                kind: ref.kind === 'view' ? 'view' : 'table',
                name: ref.name,
                database: ref.database || 'main'
            };
            const existing = findTableTab(session.id, reference);
            if (existing) {
                activate(existing.id);
                if (opts.autoRun !== false) query.run(existing);
                return existing;
            }

            const limit = Number(opts.limit) || 500;
            const sql = 'SELECT * FROM ' + U.quoteIdentifier(reference.name) +
                ' LIMIT ' + limit + ';';
            saveEditor();
            querySeq += 1;
            const tab = state.addTab({
                kind: 'table',
                title: (reference.kind === 'view' ? '视图: ' : '表: ') + reference.name,
                sessionId: session.id,
                sql: sql,
                dirty: false,
                ref: reference,
                autoLimit: limit
            });
            state.setActiveTab(tab.id);
            render();
            dom.editor.value = sql;
            updateGutter();
            updateChrome(tab);
            results.show(tab);
            if (opts.autoRun !== false) query.run(tab);
            return tab;
        }

        function closeTab(id) {
            const tab = state.tab(id);
            if (!tab) return;
            if (tab.running && tab.task) {
                tab.task.cancel();
            }
            const wasActive = state.activeTabId === id;
            if (wasActive) saveEditor();
            const index = state.tabs.indexOf(tab);
            state.removeTab(id);
            const next = state.tabs[Math.min(index, state.tabs.length - 1)];
            if (next) {
                state.setActiveTab(next.id);
                activate(next.id);
                // activate 会提前返回（activeTabId 已变化），这里再清理编辑器。
                dom.editor.value = next.sql || '';
                updateGutter();
                updateChrome(next);
                results.show(next);
            } else {
                openQuery(null, { sql: '', focus: true });
            }
            render();
        }

        function closeActive() {
            if (state.activeTabId) closeTab(state.activeTabId);
        }

        function getSelectionText() {
            if (!dom.editor) return '';
            const start = dom.editor.selectionStart;
            const end = dom.editor.selectionEnd;
            if (start == null || end == null || start === end) return '';
            return dom.editor.value.slice(start, end);
        }

        function selectRange(start, end) {
            if (!dom.editor) return;
            const text = dom.editor.value;
            const from = Math.max(0, Math.min(text.length, start));
            const to = Math.max(from, Math.min(text.length, end == null ? start : end));
            dom.editor.focus();
            dom.editor.setSelectionRange(from, to);
            const before = text.slice(0, from).split('\n').length - 1;
            dom.editor.scrollTop = Math.max(0, before * 22 - 66);
        }

        function focusEditor() {
            if (dom.editor) dom.editor.focus();
        }

        return {
            init: init,
            render: render,
            activate: activate,
            openQuery: openQuery,
            openTable: openTable,
            closeTab: closeTab,
            closeActive: closeActive,
            saveEditor: saveEditor,
            refreshRunning: refreshRunning,
            updateChrome: updateChrome,
            updateGutter: updateGutter,
            getSelectionText: getSelectionText,
            selectRange: selectRange,
            focusEditor: focusEditor
        };
    }

    window.SilkitTabs = { create: create };
})();
