// 工作台装配：连接、对象树、查询标签、结果、详情、历史之间的协作。
// M4：CodeMirror 补全、标签持久化恢复、.sql 打开/保存（M4-1/2/8）。
(function () {
    'use strict';

    const U = window.SilkitUtil;

    function create(options) {
        const api = window.SilkitApi;
        const state = window.SilkitState;
        const toast = options.toast;
        const I = window.SilkitI18n;

        const dom = {};
        let tabsPersistTimer = 0;

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
            history: null,
            hint: null,
            table: null,
            designer: null,
            settingsView: null
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
            app.tabs.showErrorAt(offset, error && error.message);
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
                if (app.hint && session) app.hint.invalidate(session.id);
            }
        };
        // 标签内容变化 → 防抖持久化（M4-2 重启恢复）。
        app.onTabChanged = function () {
            scheduleTabsPersist();
        };
        app.onTabSqlChanged = function () {
            scheduleTabsPersist();
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
            if (app.hint) app.hint.prefetch(session);

            // 重启恢复的表标签在连接建立后补上会话。
            let adopted = 0;
            state.tabs.forEach(function (tab) {
                if (tab.kind === 'table' && !tab.sessionId) {
                    tab.sessionId = session.id;
                    adopted += 1;
                }
            });
            if (adopted && state.activeTab() && state.activeTab().kind === 'table') {
                app.tabs.activate(state.activeTab().id, true);
            }

            const active = state.activeTab();
            if (active && active.kind === 'query' && !active.sessionId && isBlankSql(active.sql)) {
                active.sessionId = session.id;
                app.tabs.updateChrome(active);
                app.tabs.render();
            } else if (!(adopted && active && active.kind === 'table')) {
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
            if (app.hint) app.hint.invalidate(session.id);
            updateStatus();
        };
        app.onProfilesError = function (error) {
            console.warn('[silkit] conn.list 失败', error);
        };

        // ------------------------------------------------------------ .sql 打开/保存
        async function readSqlFromPath(path) {
            const result = await api.appReadFile(path);
            if (!result || !result.ok) {
                throw new Error((result && result.error && result.error.message) || '读取失败');
            }
            return result;
        }

        async function openSqlDialog() {
            try {
                const picked = await api.pickFile({ kind: 'sql' });
                if (!picked || !picked.path) return;
                const file = await readSqlFromPath(picked.path);
                app.tabs.openQuery(state.activeSession(), {
                    sql: file.content,
                    title: U.stripExtension(picked.path),
                    focus: true,
                    filePath: picked.path,
                    dirty: false
                });
                toast('已打开 ' + U.basename(picked.path));
            } catch (error) {
                if (error && error.code === 'cancelled') return;
                toast('打开 SQL 失败：' + (error.message || error), 'error');
            }
        }

        async function saveSqlDialog(forceDialog) {
            const tab = state.activeTab();
            if (!tab) return;
            app.tabs.saveEditor();
            const content = app.tabs.getValue();
            let path = tab.filePath;
            if (!path || forceDialog) {
                try {
                    const picked = await api.pickFile({
                        kind: 'sql',
                        save: true,
                        defaultName: (U.stripExtension(tab.title) || 'query') + '.sql'
                    });
                    if (!picked || !picked.path) return;
                    path = picked.path;
                } catch (error) {
                    if (error && error.code === 'cancelled') return;
                    toast('保存 SQL 失败：' + (error.message || error), 'error');
                    return;
                }
            }
            try {
                const result = await api.appSaveFile(path, content);
                if (!result || !result.ok) {
                    throw new Error((result && result.error && result.error.message) || '保存失败');
                }
                tab.filePath = path;
                if (tab.kind === 'query') app.tabs.setTabTitle(tab, U.stripExtension(path));
                tab.dirty = false;
                app.tabs.render();
                scheduleTabsPersist();
                toast('已保存 ' + U.basename(path));
            } catch (error) {
                toast('保存 SQL 失败：' + (error.message || error), 'error');
            }
        }

        app.openSqlDialog = openSqlDialog;
        app.saveSqlDialog = saveSqlDialog;

        // DDL 执行成功后的统一善后：刷新对象树/表标签，重命名同步标签，删除关闭标签。
        app.afterDdl = function (sessionId, info) {
            const change = info || {};
            app.tree.refreshSession(sessionId);
            if (app.hint) app.hint.invalidate(sessionId);
            const original = change.originalName || '';
            state.tabsForSession(sessionId).forEach(function (tab) {
                if (tab.kind !== 'table' || !tab.ref || !tab.ref.name) return;
                if (original && tab.ref.name !== original) return;
                if (change.dropped) {
                    app.tabs.closeTab(tab.id);
                    return;
                }
                if (change.newName && change.newName !== tab.ref.name) {
                    tab.ref.name = change.newName;
                    tab.title = '表: ' + change.newName;
                    app.tabs.updateChrome(tab);
                    app.tabs.render();
                }
                if (state.activeTab() === tab && app.table) app.table.reload(tab);
            });
            scheduleTabsPersist();
        };

        // ------------------------------------------------------------ 标签持久化
        function tabsSnapshot() {
            return state.tabs.slice(0, 50).map(function (tab) {
                return {
                    kind: tab.kind,
                    title: tab.title,
                    sql: tab.sql || '',
                    dirty: !!tab.dirty,
                    ref: tab.ref || null,
                    autoLimit: tab.autoLimit || null,
                    filePath: tab.filePath || null,
                    scroll: Number(tab.editorScroll) || 0
                };
            });
        }

        function activeTabIndex() {
            const index = state.tabs.indexOf(state.activeTab());
            return index < 0 ? 0 : index;
        }

        function scheduleTabsPersist() {
            clearTimeout(tabsPersistTimer);
            tabsPersistTimer = setTimeout(function () {
                api.settingsSet({
                    query: { tabs: tabsSnapshot(), activeTab: activeTabIndex() }
                }).catch(function (error) {
                    console.warn('[silkit] 保存标签失败', error);
                });
            }, 600);
        }

        function restoreTabs(settings) {
            const saved = settings && settings.query && Array.isArray(settings.query.tabs)
                ? settings.query.tabs
                : null;
            if (!saved || !saved.length) return false;
            const restored = [];
            saved.slice(0, 20).forEach(function (item) {
                if (!item) return;
                const isTable = item.kind === 'table' && item.ref && item.ref.name;
                if (!isTable && typeof item.sql !== 'string') return;
                restored.push(state.addTab({
                    kind: isTable ? 'table' : 'query',
                    title: item.title || (isTable ? '表: ' + item.ref.name : '查询'),
                    sessionId: null,
                    sql: isTable ? '' : item.sql,
                    dirty: isTable ? false : !!item.dirty,
                    ref: isTable ? item.ref : null,
                    filePath: isTable ? null : (item.filePath || null),
                    editorScroll: Number(item.scroll) || 0
                }));
            });
            return restored.length > 0;
        }

        app.results = window.SilkitResults.create(app);
        app.query = window.SilkitQuery.create(app);
        app.hint = window.SilkitHint ? window.SilkitHint.create(app) : null;
        app.table = window.SilkitTable ? window.SilkitTable.create(app) : null;
        app.tabs = window.SilkitTabs.create(app);
        app.detail = window.SilkitDetail.create(app);
        app.designer = window.SilkitDesigner ? window.SilkitDesigner.create(app) : null;
        app.tree = window.SilkitTree.create(app);
        app.connections = window.SilkitConnections.create(app);
        app.history = window.SilkitHistory.create(app);
        app.io = window.SilkitIo ? window.SilkitIo.create(app) : null;
        app.settingsView = window.SilkitSettings ? window.SilkitSettings.create(app) : null;

        // M8：把设置应用到运行时（语言、动效、字号、行高、侧栏宽度、编辑器选项）。
        function applySettings(settings) {
            const next = settings || {};
            state.settings = next;
            const ui = next.ui || {};
            if (I) I.setLanguage(ui.language || 'zh-CN', { notify: false });

            const shell = document.querySelector('.app');
            if (shell) shell.classList.toggle('reduce-motion', !!ui.reduceMotion);

            const root = document.documentElement;
            const width = Number(ui.sidebarWidth);
            if (Number.isFinite(width)) {
                root.style.setProperty('--sidebar-w', Math.max(180, Math.min(560, width)) + 'px');
            }
            const fontSize = Number(next.editor && next.editor.fontSize);
            if (Number.isFinite(fontSize)) {
                root.style.setProperty('--editor-font-size', Math.max(10, Math.min(22, fontSize)) + 'px');
            }

            if (app.tabs) app.tabs.applySettings(next.editor || {});
            const rowHeight = Number(ui.gridRowHeight);
            const height = Number.isFinite(rowHeight) ? rowHeight : 26;
            if (app.results) app.results.setRowHeight(height);
            if (app.table) app.table.setRowHeight(height);
            updateStatus();
        }
        app.applySettings = applySettings;

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
            const mode = byId('statusMode');
            const run = byId('runBtn');
            if (mode) {
                mode.textContent = session
                    ? (I ? I.t('status.modeConnected') : 'M8 · 已连接')
                    : (I ? I.t('status.mode') : 'M8 · 等待连接');
            }
            if (run) run.disabled = !!(state.activeTab() && state.activeTab().running);
        }

        async function start(settings) {
            dom.newConn = byId('newConnBtn');
            dom.newQuery = byId('newQueryBtn');
            dom.openSql = byId('openSqlBtn');
            dom.runSqlFile = byId('runSqlFileBtn');
            dom.treeRefresh = byId('treeRefresh');
            dom.format = byId('formatBtn');
            dom.explain = byId('explainBtn');
            dom.settings = byId('settingsBtn');

            app.results.init();
            if (app.table) app.table.init();
            app.detail.init();
            if (app.designer) app.designer.init();
            app.history.load(settings);
            // 先恢复标签，再初始化标签视图（tabs.init 在无标签时才建默认标签）。
            const restoreEnabled = !(settings && settings.ui && settings.ui.restoreTabs === false);
            const restored = restoreEnabled ? restoreTabs(settings) : false;
            app.tabs.init();
            applySettings(settings);
            if (restored) {
                const savedIndex = Number(settings && settings.query && settings.query.activeTab) || 0;
                const target = state.tabs[savedIndex];
                if (target) app.tabs.activate(target.id, true);
                toast('已恢复上次打开的标签');
            }
            app.tabs.bindDragDrop();
            app.tree.init();
            app.connections.init();
            if (app.io) app.io.init();
            if (dom.settings && app.settingsView) {
                dom.settings.addEventListener('click', function () { app.settingsView.open(); });
            }

            if (dom.newConn) dom.newConn.addEventListener('click', function () { app.connections.openDialog(); });
            if (dom.newQuery) {
                dom.newQuery.addEventListener('click', function () {
                    app.tabs.openQuery(state.activeSession(), { focus: true });
                });
            }
            if (dom.openSql) {
                dom.openSql.addEventListener('click', openSqlDialog);
            }
            if (dom.runSqlFile) {
                dom.runSqlFile.addEventListener('click', function () {
                    if (app.io) app.io.openExecuteFile();
                });
            }
            if (dom.format) {
                dom.format.addEventListener('click', function () { app.tabs.formatEditor(); });
            }
            if (dom.explain) {
                dom.explain.addEventListener('click', function () {
                    const tab = state.activeTab();
                    if (tab) app.query.explain(tab);
                });
            }
            if (dom.treeRefresh) {
                dom.treeRefresh.addEventListener('click', function () { app.tree.refreshActive(); });
            }

            state.settings = settings || {};
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
