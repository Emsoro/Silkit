// 查询历史：记录成功执行的 SQL，支持回填到新标签；写入 localStorage + settings.json。
(function () {
    'use strict';

    const U = window.SilkitUtil;
    const STORAGE_KEY = 'silkit.history';

    function create(ctx) {
        const api = ctx.api;
        const state = ctx.state;
        const toast = ctx.toast;

        const dom = {};
        let open = false;

        function byId(id) { return document.getElementById(id); }

        function init() {
            ['historyBtn', 'historyPanel', 'historyList', 'historyEmpty', 'historyClear']
                .forEach(function (id) { dom[id] = byId(id); });
            if (!dom.historyBtn) return;

            dom.historyBtn.addEventListener('click', function (event) {
                event.stopPropagation();
                toggle();
            });
            if (dom.historyClear) {
                dom.historyClear.addEventListener('click', function () {
                    state.setHistory([]);
                    persist([]);
                    render();
                    toast('查询历史已清空');
                });
            }
            if (dom.historyList) {
                dom.historyList.addEventListener('click', function (event) {
                    const item = event.target.closest('.history-item');
                    if (!item) return;
                    const index = Number(item.getAttribute('data-index'));
                    const entry = state.history[index];
                    if (!entry) return;
                    close();
                    ctx.openQuery(null, entry.sql);
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

        function load(settings) {
            const saved = settings && settings.query && Array.isArray(settings.query.history)
                ? settings.query.history
                : null;
            if (saved) {
                state.setHistory(saved);
                persist(saved);
                return;
            }
            let local = [];
            try {
                const raw = localStorage.getItem(STORAGE_KEY);
                if (raw) local = JSON.parse(raw);
            } catch (error) { /* ignore */ }
            state.setHistory(Array.isArray(local) ? local : []);
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
            persist(state.history);
        }

        function render() {
            if (!dom.historyList) return;
            U.clear(dom.historyList);
            const list = state.history;
            if (dom.historyEmpty) dom.historyEmpty.hidden = list.length > 0;
            list.forEach(function (entry, index) {
                const button = document.createElement('button');
                button.type = 'button';
                button.className = 'history-item';
                button.setAttribute('data-index', String(index));

                const head = U.el('span', 'history-item-head');
                const firstLine = String(entry.sql || '').split('\n').find(function (line) {
                    return line.trim() && !line.trim().startsWith('--');
                }) || String(entry.sql || '');
                head.appendChild(U.el('span', 'history-item-sql', firstLine.trim()));
                const meta = [U.relativeTime(entry.at)];
                if (entry.connection) meta.push(entry.connection);
                if (entry.rows) meta.push(U.formatNumber(entry.rows) + ' 行');
                if (entry.elapsedMs) meta.push(U.formatDuration(entry.elapsedMs));
                head.appendChild(U.el('span', 'history-item-meta', meta.join(' · ')));
                button.appendChild(head);
                dom.historyList.appendChild(button);
            });
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
            toggle: toggle,
            close: close,
            isOpen: function () { return open; }
        };
    }

    window.SilkitHistory = { create: create };
})();
