// 查询标签：标签栏渲染、CodeMirror 编辑器、每标签独立 Doc、错误高亮、运行按钮。
// M4：编辑器升级为 CodeMirror 5（M4-1），补全/格式化/语句定位由 editor/* 模块提供。
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
        let cm = null;
        let errorMark = null;
        let hintTimer = 0;
        let autocompleteEnabled = true;
        const tools = window.SilkitSqlTools || null;
        const hint = ctx.hint || null;

        function byId(id) { return document.getElementById(id); }

        function mimeFor(session) {
            const engine = session && session.profile ? (session.profile.engine || 'sqlite') : 'sqlite';
            if (engine === 'mysql') return 'text/x-mysql';
            if (engine === 'postgres') return 'text/x-pgsql';
            return 'text/x-sql';
        }

        function ensureDoc(tab) {
            if (!tab.doc) {
                const text = typeof tab.sql === 'string' ? tab.sql : '';
                tab.doc = window.CodeMirror.Doc(text, mimeFor(state.session(tab.sessionId)));
            }
            return tab.doc;
        }

        function init() {
            dom.tabbar = byId('tabbar');
            dom.tabAdd = byId('tabAdd');
            dom.host = byId('sqlEditor');
            dom.title = byId('editorTitle');
            dom.sub = byId('editorSub');
            dom.run = byId('runBtn');
            dom.cancel = byId('cancelBtn');
            dom.editorPane = byId('editorPane');
            dom.splitter = byId('splitter');
            dom.resultsPane = byId('resultsPane');
            dom.tablePane = byId('tablePane');
            dom.empty = byId('workspaceEmpty');

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

            dom.tabAdd.addEventListener('click', function () { openQueryFromTree(); });

            if (dom.run) {
                dom.run.addEventListener('click', function () {
                    const tab = state.activeTab();
                    if (tab) query.run(tab, runScope());
                });
            }
            if (dom.cancel) {
                dom.cancel.addEventListener('click', function () {
                    const tab = state.activeTab();
                    if (tab) query.cancel(tab);
                });
            }

            createEditor();

            // 启动时不自动建查询页：没有恢复的标签就停在空态，由用户显式新建。
            if (!state.tabs.length) {
                showEmptyState();
            } else {
                state.tabs.forEach(ensureDoc);
                activate(state.activeTabId || state.tabs[0].id);
            }
        }

        function createEditor() {
            if (!dom.host || !window.CodeMirror) {
                console.warn('[silkit] CodeMirror 未加载，编辑器降级');
                return;
            }
            cm = window.CodeMirror(dom.host, {
                value: '',
                mode: 'text/x-sql',
                lineNumbers: true,
                indentUnit: 2,
                smartIndent: true,
                matchBrackets: true,
                autoCloseBrackets: true,
                styleActiveLine: true,
                scrollbarStyle: 'native',
                lineWrapping: false,
                theme: 'silkit'
            });

            cm.on('change', function () {
                const tab = state.activeTab();
                if (!tab) return;
                if (tab.doc !== cm.getDoc()) return;
                tab.sql = cm.getValue();
                tab.dirty = true;
                refreshDirty(tab);
                if (ctx.onTabSqlChanged) ctx.onTabSqlChanged(tab);
            });

            cm.on('scroll', function () {
                const tab = state.activeTab();
                if (!tab) return;
                const info = cm.getScrollInfo();
                tab.editorScroll = info.top;
            });

            cm.on('focus', function () {
                const tab = state.activeTab();
                const session = tab && tab.sessionId ? state.session(tab.sessionId) : state.activeSession();
                if (session && hint) {
                    hint.prefetch(session, activeContext());
                }
            });

            // 输入字母/点号后延迟触发补全（M4-4）。
            cm.on('inputRead', function (editor, change) {
                if (!autocompleteEnabled) return;
                if (!change || change.origin !== '+input') return;
                if (!change.text || !change.text.length) return;
                const text = change.text[change.text.length - 1];
                if (!/[A-Za-z0-9_$.]/.test(text)) return;
                clearTimeout(hintTimer);
                hintTimer = setTimeout(function () {
                    if (!cm.hasFocus()) return;
                    const fn = hintForSession();
                    if (!fn) return;
                    cm.showHint({ hint: fn, completeSingle: false });
                }, 220);
            });

            cm.addKeyMap({
                'Ctrl-Space': function () {
                    if (!autocompleteEnabled) return;
                    const fn = hintForSession();
                    if (fn) cm.showHint({ hint: fn, completeSingle: false });
                }
            });

            cm.on('blur', function () { clearTimeout(hintTimer); });
        }

        function activeContext() {
            return ctx.queryContext ? ctx.queryContext.contextFor(state.activeTab()) : null;
        }

        function hintForSession() {
            if (!hint) return undefined;
            const tab = state.activeTab();
            const session = (tab && tab.sessionId && state.session(tab.sessionId)) ||
                state.activeSession();
            if (!session) return undefined;
            return hint.hint(session, function () { return activeContext(); });
        }

        function runScope() {
            const select = document.getElementById('runScope');
            return select ? (select.value || 'all') : 'all';
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
            syncEmptyState();
        }

        // 没有标签时隐藏全部工作区面板，只显示空态卡片；有标签时反过来。
        function syncEmptyState() {
            const empty = !state.tabs.length;
            if (dom.empty) dom.empty.hidden = !empty;
            if (!empty) return;
            if (dom.tablePane) dom.tablePane.hidden = true;
            if (dom.editorPane) dom.editorPane.hidden = true;
            if (dom.splitter) dom.splitter.hidden = true;
            if (dom.resultsPane) dom.resultsPane.hidden = true;
        }

        function showEmptyState() {
            render();
            if (dom.empty) dom.empty.hidden = false;
            if (ctx.queryContext) ctx.queryContext.sync({ force: true });
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
            if (!tab || tab.kind === 'table') return;
            if (cm) {
                tab.sql = cm.getValue();
                const info = cm.getScrollInfo();
                tab.editorScroll = info.top;
            }
        }

        function syncLayout(tab) {
            if (!state.tabs.length) { syncEmptyState(); return; }
            const isTable = !!(tab && tab.kind === 'table');
            if (dom.empty) dom.empty.hidden = true;
            if (dom.tablePane) dom.tablePane.hidden = !isTable;
            if (dom.editorPane) dom.editorPane.hidden = isTable;
            if (dom.splitter) dom.splitter.hidden = isTable;
            if (dom.resultsPane) dom.resultsPane.hidden = isTable;
            if (isTable) {
                if (ctx.table) ctx.table.show(tab);
            } else if (cm) {
                setTimeout(function () { cm.refresh(); }, 0);
            }
        }

        /// 把标签内容呈现到主区域（不改变 activeTabId）。
        function present(next) {
            ensureDoc(next);
            if (cm) {
                cm.swapDoc(next.doc);
                cm.setOption('mode', mimeFor(state.session(next.sessionId)));
                const scroll = Number(next.editorScroll) || 0;
                if (scroll > 0) cm.scrollTo(0, scroll);
                cm.refresh();
            }
            render();
            updateChrome(next);
            syncLayout(next);
            if (next.kind !== 'table') results.show(next);
        }

        function activate(id, force) {
            const next = state.tab(id);
            if (!next) return;
            if (!force && state.activeTabId === id) {
                updateChrome(next);
                syncLayout(next);
                return;
            }
            if (state.activeTabId !== id) saveEditor();
            clearErrorHighlight();
            state.setActiveTab(id);
            present(next);
            if (ctx.queryContext && next.kind === 'query') {
                ctx.queryContext.onTabActivated(next);
            }
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
                    const db = (tab.kind === 'query' && tab.database) || session.currentDb || 'main';
                    dom.sub.textContent = (profile.name || '已连接') + ' · ' + db +
                        (session.serverVersion ? ' · ' + session.serverVersion : '');
                    dom.sub.classList.remove('is-warn');
                }
            }
            if (ctx.queryContext) ctx.queryContext.sync();
            if (dom.run) {
                dom.run.hidden = !!tab.running;
                dom.run.setAttribute('aria-disabled', tab.running ? 'true' : 'false');
                dom.run.title = '执行当前 SQL（Ctrl+Enter / F5）';
            }
            if (dom.cancel) {
                dom.cancel.hidden = !tab.running;
            }
        }

        // ------------------------------------------------------------ 编辑器操作
        function setValue(text) {
            if (cm) {
                cm.setValue(String(text == null ? '' : text));
                cm.refresh();
                return;
            }
            const tab = state.activeTab();
            if (tab) { tab.sql = String(text || ''); }
        }

        function getValue() {
            if (cm) return cm.getValue();
            const tab = state.activeTab();
            return tab ? (tab.sql || '') : '';
        }

        function getSelectionText() {
            if (!cm) return '';
            return cm.somethingSelected() ? cm.getSelection() : '';
        }

        function selectRange(start, end) {
            if (!cm) return;
            const text = cm.getValue();
            const from = Math.max(0, Math.min(text.length, start));
            const to = Math.max(from, Math.min(text.length, end == null ? start : end));
            cm.setSelection(cm.posFromIndex(from), cm.posFromIndex(to));
            cm.scrollIntoView({ from: cm.posFromIndex(from), to: cm.posFromIndex(to) });
            cm.focus();
        }

        function posFromOffset(offset) {
            if (!cm) return null;
            const text = cm.getValue();
            const index = Math.max(0, Math.min(text.length, Number(offset) || 0));
            return cm.posFromIndex(index);
        }

        // 错误行高亮（M4-5）：行背景 + gutter 标记。
        function showErrorAt(offset, message) {
            if (!cm) return;
            clearErrorHighlight();
            const pos = posFromOffset(offset);
            if (!pos) return;
            cm.addLineClass(pos.line, 'background', 'cm-error-line');
            cm.addLineClass(pos.line, 'gutter', 'cm-error-gutter');
            const end = { line: pos.line, ch: cm.getLine(pos.line).length };
            errorMark = {
                line: pos.line,
                marker: cm.markText(pos, end, { className: 'cm-error-text' }),
                message: message || ''
            };
            cm.scrollIntoView(pos, 80);
        }

        function clearErrorHighlight() {
            if (!cm || !errorMark) return;
            cm.removeLineClass(errorMark.line, 'background', 'cm-error-line');
            cm.removeLineClass(errorMark.line, 'gutter', 'cm-error-gutter');
            if (errorMark.marker) errorMark.marker.clear();
            errorMark = null;
        }

        // 光标所在语句（M4-3）：返回 {start,end,sql} 或 null。
        function statementAtCursor() {
            if (!cm || !tools) return null;
            const cursor = cm.getCursor();
            const offset = cm.indexFromPos(cursor);
            const statements = tools.splitStatements(cm.getValue());
            if (!statements.length) return null;
            for (let i = 0; i < statements.length; i += 1) {
                const item = statements[i];
                if (offset >= item.start && offset <= item.end) return item;
                if (offset < item.start) return item;
            }
            return statements[statements.length - 1];
        }

        function formatEditor() {
            if (!cm || !tools) return;
            const selection = cm.somethingSelected()
                ? cm.getSelection()
                : null;
            const source = selection != null ? selection : cm.getValue();
            const formatted = tools.formatSql(source);
            if (!formatted || formatted === source) return;
            if (selection != null) {
                cm.replaceSelection(formatted);
            } else {
                const cursor = cm.getCursor();
                const scroll = cm.getScrollInfo();
                cm.setValue(formatted);
                cm.setCursor(cursor);
                cm.scrollTo(scroll.left, scroll.top);
            }
            const tab = state.activeTab();
            if (tab) {
                tab.sql = cm.getValue();
                tab.dirty = true;
                refreshDirty(tab);
            }
            toast(selection != null ? '已格式化选区' : '已格式化 SQL');
        }

        function focusEditor() {
            if (cm) cm.focus();
        }

        // 拖放 .sql 文件到编辑器打开（M4-8）。
        function bindDragDrop() {
            if (!dom.host) return;
            dom.host.addEventListener('dragover', function (event) {
                event.preventDefault();
                dom.host.classList.add('is-dragover');
            });
            dom.host.addEventListener('dragleave', function () {
                dom.host.classList.remove('is-dragover');
            });
            dom.host.addEventListener('drop', function (event) {
                dom.host.classList.remove('is-dragover');
                if (!event.dataTransfer || !event.dataTransfer.files || !event.dataTransfer.files.length) return;
                event.preventDefault();
                Array.prototype.forEach.call(event.dataTransfer.files, function (file, index) {
                    if (!/\.sql$/i.test(file.name)) return;
                    const reader = new FileReader();
                    reader.onload = function () {
                        openQuery(null, {
                            sql: String(reader.result || ''),
                            title: U.stripExtension(file.name),
                            focus: index === 0
                        });
                    };
                    reader.readAsText(file, 'utf-8');
                });
            });
        }

        // ------------------------------------------------------------ 标签创建
        function openQuery(session, options) {
            const opts = options || {};
            saveEditor();
            querySeq += 1;
            const target = session || state.activeSession();
            const tab = state.addTab({
                kind: 'query',
                title: opts.title || ('查询 ' + querySeq),
                sessionId: target ? target.id : null,
                database: opts.database || '',
                schema: opts.schema || '',
                table: opts.table || '',
                sql: opts.sql != null ? opts.sql : '',
                dirty: !!opts.dirty,
                filePath: opts.filePath || null
            });
            state.setActiveTab(tab.id);
            present(tab);
            if (ctx.queryContext) {
                ctx.queryContext.onTabOpened(tab).catch(function (error) {
                    console.warn('[silkit] 应用查询上下文失败', error);
                });
            }
            if (ctx.onTabChanged) ctx.onTabChanged(tab);
            if (opts.focus !== false && cm) cm.focus();
            return tab;
        }

        // 顶栏 / 标签栏 / Ctrl+N 新建查询：优先继承对象树当前选中的连接与库。
        function openQueryFromTree(options) {
            const picked = ctx.tree && ctx.tree.selectedContext ? ctx.tree.selectedContext() : null;
            const session = (picked && picked.session) || state.activeSession();
            const context = {
                focus: true,
                database: picked ? picked.database : '',
                schema: picked ? picked.schema : '',
                table: picked && (picked.kind === 'table' || picked.kind === 'view')
                    ? (picked.table || picked.name) : ''
            };
            return openQuery(session, Object.assign(context, options || {}));
        }

        function findTableTab(sessionId, ref) {
            for (let i = 0; i < state.tabs.length; i += 1) {
                const tab = state.tabs[i];
                if (tab.kind !== 'table' || tab.sessionId !== sessionId) continue;
                if (!tab.ref) continue;
                if (tab.ref.name === ref.name &&
                    (tab.ref.database || 'main') === (ref.database || 'main') &&
                    (tab.ref.schema || '') === (ref.schema || '')) {
                    return tab;
                }
            }
            return null;
        }

        function openTable(session, ref, options) {
            if (!session) {
                toast('请先打开连接', 'error');
                return null;
            }
            const reference = {
                kind: ref.kind === 'view' ? 'view' : 'table',
                name: ref.name,
                database: ref.database || 'main',
                schema: ref.schema || ''
            };
            const existing = findTableTab(session.id, reference);
            if (existing) {
                activate(existing.id, true);
                return existing;
            }

            saveEditor();
            const tab = state.addTab({
                kind: 'table',
                title: (reference.kind === 'view' ? '视图: ' : '表: ') + reference.name,
                sessionId: session.id,
                sql: '',
                dirty: false,
                ref: reference
            });
            state.setActiveTab(tab.id);
            present(tab);
            if (ctx.onTabChanged) ctx.onTabChanged(tab);
            return tab;
        }

        async function closeTab(id) {
            const tab = state.tab(id);
            if (!tab) return;
            if (tab.running && tab.task) {
                tab.task.cancel();
            }
            if (tab.kind === 'table') {
                if (ctx.table && ctx.table.hasChanges(tab) &&
                    !(await U.confirm({
                        title: '关闭标签',
                        message: '表「' + ((tab.ref && tab.ref.name) || tab.title) +
                            '」还有未保存的修改，仍要关闭？',
                        hint: '未保存的增删改会丢失。',
                        okText: '关闭',
                        danger: true
                    }))) {
                    return;
                }
                if (ctx.table) ctx.table.close(tab);
            }
            const wasActive = state.activeTabId === id;
            if (wasActive && tab.kind !== 'table') saveEditor();
            state.removeTab(id);
            if (ctx.onTabChanged) ctx.onTabChanged(tab);
            // 关闭最后一个标签后停在空态，不再自动补建一个查询页。
            if (state.tabs.length) {
                const next = state.activeTab() || state.tabs[0];
                activate(next.id, true);
            } else {
                showEmptyState();
            }
        }

        function closeActive() {
            if (state.activeTabId) closeTab(state.activeTabId);
        }

        function setTabTitle(tab, title) {
            if (!tab) return;
            tab.title = title;
            render();
        }

        // 片段插入光标处（M4-7）。
        function insertSnippet(sql) {
            if (!cm) return;
            const text = String(sql || '');
            cm.replaceSelection(text, 'end');
            const tab = state.activeTab();
            if (tab) {
                tab.sql = cm.getValue();
                tab.dirty = true;
                refreshDirty(tab);
            }
            cm.focus();
        }

        function applySettings(settings) {
            const editor = settings || {};
            autocompleteEnabled = editor.autocomplete !== false;
            if (!cm) return;
            const tab_size = Number(editor.tabSize) || 2;
            if (editor.showLineNumbers !== undefined) {
                cm.setOption('lineNumbers', !!editor.showLineNumbers);
            }
            if (editor.wordWrap !== undefined) {
                cm.setOption('lineWrapping', !!editor.wordWrap);
            }
            cm.setOption('indentUnit', tab_size);
            cm.setOption('tabSize', tab_size);
            cm.refresh();
        }

        return {
            init: init,
            render: render,
            activate: activate,
            applySettings: applySettings,
            openQuery: openQuery,
            openQueryFromTree: openQueryFromTree,
            openTable: openTable,
            closeTab: closeTab,
            closeActive: closeActive,
            saveEditor: saveEditor,
            refreshRunning: refreshRunning,
            updateChrome: updateChrome,
            getSelectionText: getSelectionText,
            selectRange: selectRange,
            statementAtCursor: statementAtCursor,
            formatEditor: formatEditor,
            showErrorAt: showErrorAt,
            clearErrorHighlight: clearErrorHighlight,
            setValue: setValue,
            getValue: getValue,
            setTabTitle: setTabTitle,
            insertSnippet: insertSnippet,
            focusEditor: focusEditor,
            bindDragDrop: bindDragDrop
        };
    }

    window.SilkitTabs = { create: create };
})();
