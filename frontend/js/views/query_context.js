// 查询上下文：查询页「连接 / 库」下拉、自动 USE、按标签记忆库与 schema（v1.0.2）。
//
// 设计要点：
//   * 每个 query 标签保存自己的 sessionId / database / schema / table，切换标签时自动把会话
//     切回该标签的库（MySQL 执行 USE，PG 走 conn.set_database 重连，SQLite 只接受已 ATTACH 的库）。
//   * 新建查询时优先采用对象树当前选中的节点所属连接与库。
//   * 库切换后清空补全元数据缓存并刷新对象树，避免提示仍是上一个库的表/列。
(function () {
    'use strict';

    function create(ctx) {
        const api = ctx.api;
        const state = ctx.state;
        const toast = ctx.toast;

        let domSession = null;
        let domDatabase = null;
        let lastSyncKey = '';
        let fillSeq = 0;
        const dbCache = new Map();   // sessionId -> { list: [{name, note}], at }
        const switches = new Map();  // sessionId -> Promise

        function byId(id) { return document.getElementById(id); }

        function t(key, fallback) {
            const I = window.SilkitI18n;
            const value = I ? I.t(key) : '';
            return value && value !== key ? value : fallback;
        }

        function engineOf(session) {
            return (session && session.profile && session.profile.engine) ||
                (session && session.engine) || 'sqlite';
        }

        function currentDbOf(session) {
            if (!session) return '';
            return session.currentDb ||
                (session.profile && session.profile.database) ||
                (engineOf(session) === 'sqlite' ? 'main' : '');
        }

        // 补全模块也使用这里返回的上下文，保证「所见库」与「提示来源」一致。
        function contextFor(tab) {
            const item = tab || state.activeTab();
            let session = item && item.sessionId ? state.session(item.sessionId) : null;
            if (!session && item && !item.sessionId) session = state.activeSession();
            if (!session) return { session: null, database: '', schema: '', table: '', engine: 'sqlite' };
            const engine = engineOf(session);
            return {
                session: session,
                engine: engine,
                database: (item && item.database) || currentDbOf(session) ||
                    (engine === 'sqlite' ? 'main' : ''),
                schema: (item && item.schema) || (engine === 'postgres'
                    ? ((session.profile && session.profile.schema) || 'public') : ''),
                table: (item && item.table) || ''
            };
        }

        function option(value, label) {
            const node = document.createElement('option');
            node.value = value;
            node.textContent = label;
            return node;
        }

        // ------------------------------------------------------------ 库列表
        async function databasesOf(session, force) {
            const cached = dbCache.get(session.id);
            if (!force && cached && Date.now() - cached.at < 30000) return cached.list;
            const engine = engineOf(session);
            const parent = engine === 'postgres'
                ? { kind: 'folder', name: 'databases', database: currentDbOf(session) }
                : { kind: 'connection' };
            let list = [];
            try {
                const data = await api.metaChildren(session.id, parent);
                const nodes = data && Array.isArray(data.nodes) ? data.nodes : [];
                list = nodes.filter(function (node) {
                    return node && node.name && (node.kind === 'database' || node.kind === 'folder');
                }).map(function (node) {
                    return { name: node.name, note: node.badge || '' };
                });
            } catch (error) {
                console.warn('[silkit] 读取数据库列表失败', error);
            }
            const current = currentDbOf(session);
            if (current && !list.some(function (item) { return item.name === current; })) {
                list.unshift({ name: current, note: '' });
            }
            if (!list.length) list = [{ name: current || (engine === 'sqlite' ? 'main' : ''), note: '' }];
            dbCache.set(session.id, { list: list, at: Date.now() });
            return list;
        }

        function fillDatabaseOptions(session, selected, force) {
            if (!domDatabase) return;
            const seq = ++fillSeq;
            const sessionId = session ? session.id : '';
            domDatabase.innerHTML = '';
            if (!session) {
                domDatabase.appendChild(option('', t('editor.noDatabase', '无可用库')));
                domDatabase.disabled = true;
                return;
            }
            domDatabase.disabled = false;
            domDatabase.appendChild(option(selected || '', selected || t('editor.noDatabase', '无可用库')));
            databasesOf(session, !!force).then(function (list) {
                if (seq !== fillSeq || !domDatabase) return;
                const active = contextFor(state.activeTab());
                if (!active.session || active.session.id !== sessionId) return;
                const value = active.database || selected;
                domDatabase.innerHTML = '';
                list.forEach(function (item) {
                    domDatabase.appendChild(option(item.name, item.note
                        ? item.name + ' · ' + item.note : item.name));
                });
                if (value && !list.some(function (item) { return item.name === value; })) {
                    domDatabase.appendChild(option(value, value));
                }
                domDatabase.value = value || '';
            }).catch(function () { /* 保留当前库即可 */ });
        }

        // ------------------------------------------------------------ 同步下拉
        function sync(options) {
            if (!domSession || !domDatabase) return;
            const force = !!(options && options.force);
            const tab = state.activeTab();
            const context = contextFor(tab);
            const sessions = Array.from(state.sessions.values());
            const key = [tab ? tab.id : '', context.session ? context.session.id : '',
                context.database, sessions.length].join('|');
            if (!force && key === lastSyncKey) return;
            lastSyncKey = key;

            domSession.innerHTML = '';
            if (!sessions.length) {
                domSession.appendChild(option('', t('editor.noSession', '未连接')));
            } else {
                sessions.forEach(function (session) {
                    const profile = session.profile || {};
                    const label = profile.name || engineOf(session);
                    domSession.appendChild(option(session.id, label));
                });
                domSession.appendChild(option('__open__', '＋ ' + t('editor.openConnection', '打开连接…')));
            }
            domSession.value = context.session ? context.session.id : '';
            domSession.disabled = false;

            fillDatabaseOptions(context.session, context.database, force);
        }

        // ------------------------------------------------------------ 自动 USE
        async function switchDatabase(session, database) {
            if (!session || !database) return database || '';
            if (String(database) === String(currentDbOf(session))) return currentDbOf(session);
            const running = switches.get(session.id);
            if (running) {
                try { await running; } catch (error) { /* 由本次调用重新尝试 */ }
            }
            const task = (async function () {
                const result = await api.connSetDatabase(session.id, database);
                const next = (result && result.currentDb) || database;
                session.currentDb = next;
                dbCache.delete(session.id);
                if (ctx.hint) ctx.hint.invalidate(session.id);
                if (ctx.tree && ctx.tree.refreshSession) ctx.tree.refreshSession(session.id);
                state.emit('session-database', session);
                return next;
            })();
            switches.set(session.id, task);
            try {
                return await task;
            } finally {
                switches.delete(session.id);
            }
        }

        // 标签激活 / 执行 SQL 前调用：库不一致就自动切换。
        async function ensureDatabase(tab) {
            if (!tab || tab.kind !== 'query') return true;
            const context = contextFor(tab);
            const session = context.session;
            if (!session) return true;
            if (!tab.database) tab.database = context.database;
            if (String(tab.database) === String(currentDbOf(session))) return true;
            try {
                const next = await switchDatabase(session, tab.database);
                tab.database = next;
                if (ctx.tabs) ctx.tabs.updateChrome(tab);
                sync({ force: true });
                toast(t('editor.switchedDb', '已切换数据库') + ' · ' + next);
                return true;
            } catch (error) {
                toast(t('editor.switchDbFailed', '切换数据库失败') + '：' +
                    ((error && error.message) || error), 'error');
                tab.database = currentDbOf(session);   // 回退，避免每次执行都重试失败
                sync({ force: true });
                return false;
            }
        }

        async function setSession(value) {
            const tab = state.activeTab();
            if (!tab || tab.kind !== 'query') return;
            if (value === '__open__') {
                if (ctx.connections) ctx.connections.openDialog();
                sync({ force: true });
                return;
            }
            const session = state.session(value);
            if (!session) { sync({ force: true }); return; }
            tab.sessionId = session.id;
            tab.database = currentDbOf(session);
            tab.schema = '';
            tab.table = '';
            state.setActiveSession(session.id);
            if (ctx.tabs) {
                ctx.tabs.updateChrome(tab);
                ctx.tabs.render();
            }
            if (ctx.hint) ctx.hint.prefetch(session, contextFor(tab));
            sync({ force: true });
            await ensureDatabase(tab);
            if (ctx.onTabChanged) ctx.onTabChanged(tab);
        }

        async function setDatabase(value) {
            const tab = state.activeTab();
            if (!tab || tab.kind !== 'query' || !value) return;
            tab.database = value;
            if (ctx.hint && tab.sessionId) ctx.hint.invalidate(tab.sessionId);
            sync({ force: true });
            await ensureDatabase(tab);
            if (ctx.tabs) ctx.tabs.updateChrome(tab);
            if (ctx.onTabChanged) ctx.onTabChanged(tab);
        }

        async function onTabOpened(tab) {
            const context = contextFor(tab);
            tab.database = tab.database || context.database;
            if (ctx.hint && context.session) ctx.hint.prefetch(context.session, contextFor(tab));
            sync({ force: true });
            if (context.session) await ensureDatabase(tab);
            if (ctx.tabs) ctx.tabs.updateChrome(tab);
        }

        function onTabActivated(tab) {
            sync({ force: true });
            if (tab && tab.kind === 'query') {
                ensureDatabase(tab).catch(function () { /* 已在内部提示 */ });
            }
        }

        function invalidate(sessionId) {
            if (sessionId) dbCache.delete(sessionId);
            else dbCache.clear();
            sync({ force: true });
        }

        function init() {
            domSession = byId('querySessionSelect');
            domDatabase = byId('queryDatabaseSelect');
            if (domSession) {
                domSession.addEventListener('change', function () {
                    setSession(domSession.value).catch(function (error) {
                        toast((error && error.message) || String(error), 'error');
                    });
                });
            }
            if (domDatabase) {
                domDatabase.addEventListener('change', function () {
                    setDatabase(domDatabase.value).catch(function (error) {
                        toast((error && error.message) || String(error), 'error');
                    });
                });
            }
            state.on('sessions', function () { invalidate(); });
            state.on('active-session', function () { sync(); });
            state.on('active-tab', function (tab) { onTabActivated(tab); });
            state.on('session-database', function () { sync({ force: true }); });
            const I = window.SilkitI18n;
            if (I && I.onChange) I.onChange(function () { sync({ force: true }); });
            sync({ force: true });
        }

        return {
            init: init,
            sync: sync,
            contextFor: contextFor,
            ensureDatabase: ensureDatabase,
            onTabOpened: onTabOpened,
            onTabActivated: onTabActivated,
            invalidate: invalidate,
            databasesOf: databasesOf
        };
    }

    window.SilkitQueryContext = { create: create };
})();
