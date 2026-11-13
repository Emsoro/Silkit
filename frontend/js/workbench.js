// 工作台装配：连接、对象树、查询标签、结果、详情、历史之间的协作。
(function () {
    'use strict';

    function create(options) {
        const api = window.SilkitApi;
        const state = window.SilkitState;
        const toast = options.toast;

        const dom = {};

        const app = {
            api: api,
            state: state,
            toast: toast,
            results: null,
            query: null,
            tabs: null,
            detail: null,
            tree: null,
            connections: null,
            history: null
        };

        app.showDetail = function (sessionId, ref) {
            return app.detail.showDetail(sessionId, ref);
        };
        app.showDdl = function (sessionId, ref) {
            return app.detail.showDdl(sessionId, ref);
        };
        app.openTable = function (session, ref) {
            return app.tabs.openTable(session, ref);
        };
        app.openQuery = function (session, sql) {
            return app.tabs.openQuery(session || state.activeSession(), {
                sql: sql || '',
                focus: true
            });
        };
        app.focusConnections = function () {
            app.connections.openDialog();
        };
        app.onErrorPosition = function (position, error) {
            const tab = state.activeTab();
            if (!tab) return;
            const offset = typeof error.offset === 'number' ? error.offset : -1;
            if (offset < 0) return;
            const length = Math.max(1, Math.min(80, (tab.runSql || '').length - offset));
            app.tabs.selectRange(offset, offset + length);
        };
        app.onRunningChanged = function (tab, running) {
            app.tabs.refreshRunning(tab);
            updateStatus();
            const run = document.getElementById('runBtn');
            if (run && state.activeTab() === tab) run.disabled = !!running;
        };
        app.onExecuted = function (sql, session, result) {
            app.history.record(sql, session, result);
            if (isSchemaChange(sql)) {
                app.tree.refreshSession(session.id);
            }
        };
        app.onOpen = function (session) {
            const existing = state.sessionForProfile(session.profile && session.profile.id);
            if (existing && existing.id !== session.id) {
                api.connClose(session.id).catch(function () { /* ignore */ });
                state.setActiveSession(existing.id);
                toast('该连接已经打开');
                return;
            }

            state.addSession(session);
            app.tree.addSession(session);

            const active = state.activeTab();
            if (active && active.kind === 'query' && !active.sessionId && isBlankSql(active.sql)) {
                active.sessionId = session.id;
                app.tabs.updateChrome(active);
                app.tabs.render();
            } else {
                app.tabs.openQuery(session, { focus: true });
            }
            updateStatus();
        };
        app.onClosed = function (session) {
            // 主动断开与后端 session.closed 事件可能都到达，清理必须幂等。
            if (!state.session(session.id)) return;
            const tabs = state.tabsForSession(session.id);
            tabs.forEach(function (tab) { app.tabs.closeTab(tab.id); });
            app.tree.removeSession(session.id);
            state.removeSession(session.id);
            app.tree.updateStatus();
            updateStatus();
        };
        app.onProfilesError = function (error) {
            console.warn('[silkit] conn.list 失败', error);
        };

        app.results = window.SilkitResults.create(app);
        app.query = window.SilkitQuery.create(app);
        app.tabs = window.SilkitTabs.create(app);
        app.detail = window.SilkitDetail.create(app);
        app.tree = window.SilkitTree.create(app);
        app.connections = window.SilkitConnections.create(app);
        app.history = window.SilkitHistory.create(app);

        function byId(id) { return document.getElementById(id); }

        function isSchemaChange(sql) {
            return /(^|;)\s*(create|drop|alter|attach|detach|reindex|vacuum)\b/i.test(String(sql || ''));
        }

        function isBlankSql(sql) {
            return String(sql || '')
                .replace(/\/\*[\s\S]*?\*\//g, ' ')
                .replace(/--[^\n]*/g, ' ')
                .trim() === '';
        }

        function updateStatus() {
            const session = state.activeSession();
            const dot = byId('connStatus');
            const label = byId('connLabel');
            const mode = byId('statusMode');
            const run = byId('runBtn');
            if (dot) {
                dot.className = 'dot ' + (session ? 'dot-ok' : 'dot-muted');
            }
            if (label) {
                if (session) {
                    const profile = session.profile || {};
                    label.textContent = (profile.name || '已连接') +
                        (session.currentDb ? ' · ' + session.currentDb : '') +
                        (session.readOnly ? ' · 只读' : '');
                    label.title = (profile.file || '') + '\nSQLite ' + (session.serverVersion || '');
                } else {
                    label.textContent = '未连接';
                    label.title = '文件 · 新建连接';
                }
            }
            if (mode) {
                mode.textContent = session ? 'M1 · SQLite' : 'M1 · 等待连接';
            }
            if (run) run.disabled = !!(state.activeTab() && state.activeTab().running);
        }

        async function start(settings) {
            dom.newConn = byId('newConnBtn');
            dom.newQuery = byId('newQueryBtn');
            dom.openSql = byId('openSqlBtn');
            dom.treeRefresh = byId('treeRefresh');

            app.results.init();
            app.detail.init();
            app.tabs.init();
            app.tree.init();
            app.connections.init();
            app.history.init();

            if (dom.newConn) dom.newConn.addEventListener('click', function () { app.connections.openDialog(); });
            if (dom.newQuery) {
                dom.newQuery.addEventListener('click', function () {
                    app.tabs.openQuery(state.activeSession(), { focus: true });
                });
            }
            if (dom.openSql) {
                dom.openSql.addEventListener('click', function () {
                    toast('SQL 文件导入将在 M4 接入');
                });
            }
            if (dom.treeRefresh) {
                dom.treeRefresh.addEventListener('click', function () { app.tree.refreshActive(); });
            }

            state.settings = settings || {};
            app.history.load(state.settings);

            state.on('active-session', updateStatus);
            state.on('active-tab', updateStatus);
            state.on('tab-updated', function () { updateStatus(); });

            // 后端主动关闭会话时的兜底清理；前端主动断开时会话已被移除，直接跳过。
            window.addEventListener('silkit:session-closed', function (event) {
                const id = event.detail && event.detail.sessionId;
                const session = id ? state.session(id) : null;
                if (session) app.onClosed(session);
            });

            updateStatus();
            await app.connections.loadProfiles();
            updateStatus();
            return app;
        }

        return {
            app: app,
            start: start,
            updateStatus: updateStatus,
            openCell: function (info) { app.results.openCell(info); }
        };
    }

    window.SilkitWorkbench = { create: create };
})();
