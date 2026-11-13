// 查询历史与片段：记录、搜索、回填；双写 localStorage + settings.json（M4-7）。
(function () {
    'use strict';

    const U = window.SilkitUtil;
    const STORAGE_KEY = 'silkit.history';
    const SNIPPET_KEY = 'silkit.snippets';
    const HISTORY_CAP = 5000;
    const SNIPPET_CAP = 200;

    function create(ctx) {
        const api = ctx.api;
        const state = ctx.state;
        const toast = ctx.toast;

        const dom = {};
        let open = false;
        let mode = 'recent';
        let filter = '';

        function byId(id) { return document.getElementById(id); }

        function snippets() {
            return Array.isArray(state.snippets) ? state.snippets : [];
        }

        function init() {
            ['historyBtn', 'historyPanel', 'historyList', 'historyClear', 'historyFilter',
                'historyTabRecent', 'historyTabSnippets', 'snippetList', 'snippetAddBtn',
                'historyHeadHint', 'historyCount']
                .forEach(function (id) { dom[id] = byId(id); });
            if (!dom.historyBtn) return;

            dom.historyBtn.addEventListener('click', function (event) {
                event.stopPropagation();
                toggle();
            });
            if (dom.historyClear) {
                dom.historyClear.addEventListener('click', function () {
                    if (mode === 'snippets') {
                        state.snippets = [];
                        persistSnippets();
                        render();
                        toast('片段已清空');
                    } else {
                        state.setHistory([]);
                        persist([]);
                        render();
                        toast('查询历史已清空');
                    }
                });
            }
            if (dom.historyFilter) {
                dom.historyFilter.addEventListener('input', function () {
                    filter = dom.historyFilter.value.trim().toLowerCase();
                    render();
                });
            }
            if (dom.historyTabRecent) {
                dom.historyTabRecent.addEventListener('click', function () { setMode('recent'); });
            }
            if (dom.historyTabSnippets) {
                dom.historyTabSnippets.addEventListener('click', function () { setMode('snippets'); });
            }
            if (dom.snippetAddBtn) {
                dom.snippetAddBtn.addEventListener('click', saveSnippetFromEditor);
            }

            if (dom.historyList) {
                dom.historyList.addEventListener('click', function (event) {
                    const item = event.target.closest('.history-item');
                    if (!item) return;
                    const index = Number(item.getAttribute('data-index'));
                    if (mode === 'snippets') {
                        const snippet = snippets()[index];
                        if (!snippet) return;
                        close();
                        insertSnippet(snippet.sql);
                    } else {
                        const entry = state.history[index];
                        if (!entry) return;
                        close();
                        ctx.openQuery(null, entry.sql);
                    }
                });
            }
            document.addEventListener('pointerdown', function (event) {
                if (!open) return;
                if (event.target.closest('#historyPanel') || event.target.closest('#historyBtn')) return;
                close();
            });
            document.addEventListener('keydown', function (event) {
                if (event.key === 'Escape' && open) close();
            });
            state.on('history', function () {
                if (open) render();
            });
            render();
        }

        function setMode(value) {
            mode = value;
            if (dom.historyTabRecent) dom.historyTabRecent.classList.toggle('is-active', mode === 'recent');
            if (dom.historyTabSnippets) dom.historyTabSnippets.classList.toggle('is-active', mode === 'snippets');
            if (dom.historyHeadHint) {
                dom.historyHeadHint.textContent = mode === 'snippets'
                    ? '点击片段插入光标处'
                    : '成功执行后自动记录';
            }
            if (dom.historyClear) dom.historyClear.textContent = mode === 'snippets' ? '清空片段' : '清空';
            if (dom.snippetAddBtn) dom.snippetAddBtn.hidden = mode !== 'snippets';
            if (dom.snippetList) dom.snippetList.hidden = mode !== 'snippets';
            if (dom.historyList) dom.historyList.hidden = mode === 'snippets';
            render();
        }

        // ------------------------------------------------------------ 持久化
        function load(settings) {
            const saved = settings && settings.query && Array.isArray(settings.query.history)
                ? settings.query.history
                : null;
            if (saved) {
                state.setHistory(saved);
                persist(saved);
            } else {
                let local = [];
                try {
                    const raw = localStorage.getItem(STORAGE_KEY);
                    if (raw) local = JSON.parse(raw);
                } catch (error) { /* ignore */ }
                state.setHistory(Array.isArray(local) ? local : []);
            }

            let snippetsSaved = null;
            if (settings && settings.query && Array.isArray(settings.query.snippets)) {
                snippetsSaved = settings.query.snippets;
            } else {
                try {
                    const raw = localStorage.getItem(SNIPPET_KEY);
                    if (raw) snippetsSaved = JSON.parse(raw);
                } catch (error) { /* ignore */ }
            }
            state.snippets = Array.isArray(snippetsSaved) ? snippetsSaved : [];
            persistSnippets();
            render();
        }

        function persist(list) {
            try {
                localStorage.setItem(STORAGE_KEY, JSON.stringify(list));
            } catch (error) { /* ignore */ }
            api.settingsSet({ query: { history: list } }).catch(function (error) {
                console.warn('[silkit] 保存查询历史失败', error);
            });
        }

        function persistSnippets() {
            const list = snippets();
            try {
                localStorage.setItem(SNIPPET_KEY, JSON.stringify(list));
            } catch (error) { /* ignore */ }
            api.settingsSet({ query: { snippets: list } }).catch(function (error) {
                console.warn('[silkit] 保存片段失败', error);
            });
        }

        function record(sql, session, result) {
            const text = String(sql || '').trim();
            if (!text) return;
            const profile = session && session.profile ? session.profile : {};
            state.addHistory({
                sql: text,
                at: new Date().toISOString(),
                engine: (profile.engine || 'sqlite'),
                connection: profile.name || '',
                rows: result && result.statements
                    ? result.statements.reduce(function (sum, item) { return sum + (Number(item.rows) || 0); }, 0)
                    : 0,
                elapsedMs: result && result.elapsedMs ? result.elapsedMs : 0
            });
            // addHistory 上限为 60，这里放宽到 HISTORY_CAP。
            if (state.history.length > HISTORY_CAP) {
                state.setHistory(state.history.slice(0, HISTORY_CAP));
            }
            persist(state.history);
        }

        // ------------------------------------------------------------ 片段
        function saveSnippetFromEditor() {
            const tabs = ctx.tabs;
            if (!tabs) return;
            const selection = tabs.getSelectionText ? tabs.getSelectionText() : '';
            const sql = (selection && selection.trim())
                ? selection
                : (tabs.getValue ? tabs.getValue() : '');
            if (!sql.trim()) {
                toast('编辑器为空，没有可保存的片段', 'error');
                return;
            }
            addSnippet(sql.trim());
            toast('片段已保存');
            setMode('snippets');
        }

        function addSnippet(sql, title) {
            const list = snippets().slice();
            const text = String(sql || '').trim();
            if (!text) return;
            for (let i = list.length - 1; i >= 0; i -= 1) {
                if (list[i].sql === text) list.splice(i, 1);
            }
            list.unshift({
                sql: text,
                title: title || (text.split('\n').find(function (line) {
                    return line.trim() && !line.trim().startsWith('--');
                }) || text).slice(0, 60),
                at: new Date().toISOString()
            });
            state.snippets = list.slice(0, SNIPPET_CAP);
            persistSnippets();
            render();
        }

        function insertSnippet(sql) {
            const tabs = ctx.tabs;
            if (!tabs) return;
            if (tabs.insertSnippet) tabs.insertSnippet(sql);
            else if (tabs.setValue) tabs.setValue(sql);
            toast('片段已插入');
        }

        // ------------------------------------------------------------ 渲染
        function matchFilter(text) {
            if (!filter) return true;
            return String(text || '').toLowerCase().indexOf(filter) >= 0;
        }

        function render() {
            if (!dom.historyList) return;
            const isSnippets = mode === 'snippets';
            const target = isSnippets ? dom.snippetList : dom.historyList;
            U.clear(dom.historyList);
            U.clear(dom.snippetList);
            if (isSnippets) {
                const list = snippets().filter(function (item) {
                    return matchFilter(item.sql) || matchFilter(item.title);
                });
                list.forEach(function (item) {
                    target.appendChild(itemNode({
                        sql: item.sql,
                        at: item.at,
                        meta: [item.title || '片段'].filter(Boolean)
                    }, list.indexOf(item)));
                });
                if (dom.historyCount) dom.historyCount.textContent =
                    list.length + ' / ' + snippets().length + ' 条';
            } else {
                const all = state.history;
                const list = all.filter(function (entry) { return matchFilter(entry.sql); });
                list.forEach(function (entry, index) {
                    const meta = [U.relativeTime(entry.at)];
                    if (entry.connection) meta.push(entry.connection);
                    if (entry.engine) meta.push(entry.engine);
                    if (entry.rows) meta.push(U.formatNumber(entry.rows) + ' 行');
                    if (entry.elapsedMs) meta.push(U.formatDuration(entry.elapsedMs));
                    target.appendChild(itemNode(entry, all.indexOf(entry), meta));
                });
                if (dom.historyCount) dom.historyCount.textContent =
                    list.length + ' / ' + all.length + ' 条';
            }
        }

        function itemNode(entry, index, metaExtra) {
            const button = document.createElement('button');
            button.type = 'button';
            button.className = 'history-item';
            button.setAttribute('data-index', String(index));
            const head = U.el('span', 'history-item-head');
            const firstLine = String(entry.sql || '').split('\n').find(function (line) {
                return line.trim() && !line.trim().startsWith('--');
            }) || String(entry.sql || '');
            head.appendChild(U.el('span', 'history-item-sql', firstLine.trim()));
            head.appendChild(U.el('span', 'history-item-meta',
                (metaExtra || []).filter(Boolean).join(' · ')));
            button.appendChild(head);
            button.title = String(entry.sql || '').slice(0, 500);
            return button;
        }

        function position() {
            if (!dom.historyPanel || !dom.historyBtn) return;
            const rect = dom.historyBtn.getBoundingClientRect();
            const width = dom.historyPanel.offsetWidth || 380;
            const left = Math.max(8, Math.min(window.innerWidth - width - 12, rect.right - width));
            dom.historyPanel.style.left = left + 'px';
            dom.historyPanel.style.top = (rect.bottom + 6) + 'px';
            dom.historyPanel.style.right = 'auto';
        }

        function show() {
            if (!dom.historyPanel) return;
            render();
            dom.historyPanel.hidden = false;
            position();
            open = true;
            dom.historyBtn.setAttribute('aria-expanded', 'true');
            if (dom.historyFilter) dom.historyFilter.focus();
        }

        function close() {
            if (!dom.historyPanel) return;
            dom.historyPanel.hidden = true;
            open = false;
            dom.historyBtn.setAttribute('aria-expanded', 'false');
        }

        function toggle() {
            if (open) close();
            else show();
        }

        return {
            init: init,
            load: load,
            record: record,
            addSnippet: addSnippet,
            toggle: toggle,
            close: close,
            isOpen: function () { return open; }
        };
    }

    window.SilkitHistory = { create: create };
})();
