// 前端工作台状态：连接会话、查询标签、连接配置与查询历史。
// 所有 UI 模块共享这一份状态；状态变化通过事件通知。
(function () {
    'use strict';

    const listeners = new Map();

    const state = {
        sessions: new Map(),       // sessionId -> session
        activeSessionId: null,
        profiles: [],              // conn.list 返回的已保存连接（无密码）
        tabs: [],
        activeTabId: null,
        history: [],
        settings: {},
        seq: { tab: 0, session: 0 }
    };

    function on(event, callback) {
        if (typeof callback !== 'function') return function () {};
        if (!listeners.has(event)) listeners.set(event, []);
        listeners.get(event).push(callback);
        return function () {
            const list = listeners.get(event) || [];
            listeners.set(event, list.filter(function (fn) { return fn !== callback; }));
        };
    }

    function emit(event, payload) {
        const list = listeners.get(event) || [];
        list.slice().forEach(function (callback) {
            try {
                callback(payload);
            } catch (error) {
                console.error('[silkit] state listener ' + event, error);
            }
        });
    }

    // ------------------------------------------------------------ 会话
    function addSession(session) {
        const item = session || {};
        if (!item.id) item.id = 's' + Date.now().toString(36) + '-' + (++state.seq.session);
        item.openedAt = new Date().toISOString();
        state.sessions.set(item.id, item);
        state.activeSessionId = item.id;
        emit('sessions', { type: 'add', session: item });
        emit('active-session', item);
        return item;
    }

    function removeSession(id) {
        const session = state.sessions.get(id);
        if (!session) return null;
        state.sessions.delete(id);
        if (state.activeSessionId === id) {
            state.activeSessionId = state.sessions.size
                ? state.sessions.values().next().value.id
                : null;
        }
        emit('sessions', { type: 'remove', session: session });
        emit('active-session', activeSession());
        return session;
    }

    function session(id) {
        return state.sessions.get(id) || null;
    }

    function activeSession() {
        return state.activeSessionId ? state.sessions.get(state.activeSessionId) || null : null;
    }

    function setActiveSession(id) {
        if (id && !state.sessions.has(id)) return null;
        state.activeSessionId = id || null;
        const current = activeSession();
        emit('active-session', current);
        return current;
    }

    function sessionForProfile(profileId) {
        if (!profileId) return null;
        let found = null;
        state.sessions.forEach(function (item) {
            if (!found && item.profile && item.profile.id === profileId) found = item;
        });
        return found;
    }

    // ------------------------------------------------------------ 标签
    function addTab(tab) {
        const item = tab || {};
        item.id = item.id || 't' + Date.now().toString(36) + '-' + (++state.seq.tab);
        if (!item.kind) item.kind = 'query';
        if (typeof item.sql !== 'string') item.sql = '';
        if (!Array.isArray(item.messages)) item.messages = [];
        item.dirty = !!item.dirty;
        item.running = false;
        item.task = null;
        item.result = null;
        state.tabs.push(item);
        emit('tabs', { type: 'add', tab: item });
        return item;
    }

    function tab(id) {
        for (let i = 0; i < state.tabs.length; i += 1) {
            if (state.tabs[i].id === id) return state.tabs[i];
        }
        return null;
    }

    function activeTab() {
        return state.activeTabId ? tab(state.activeTabId) : null;
    }

    function setActiveTab(id) {
        if (id && !tab(id)) return null;
        state.activeTabId = id || null;
        emit('active-tab', activeTab());
        return activeTab();
    }

    function updateTab(id, patch) {
        const item = tab(id);
        if (!item) return null;
        Object.keys(patch || {}).forEach(function (key) { item[key] = patch[key]; });
        emit('tab-updated', item);
        return item;
    }

    function removeTab(id) {
        for (let i = 0; i < state.tabs.length; i += 1) {
            if (state.tabs[i].id !== id) continue;
            const removed = state.tabs.splice(i, 1)[0];
            if (state.activeTabId === id) {
                const next = state.tabs[Math.min(i, state.tabs.length - 1)] || null;
                state.activeTabId = next ? next.id : null;
            }
            emit('tabs', { type: 'remove', tab: removed });
            emit('active-tab', activeTab());
            return removed;
        }
        return null;
    }

    function tabsForSession(sessionId) {
        return state.tabs.filter(function (item) { return item.sessionId === sessionId; });
    }

    // ------------------------------------------------------------ 历史
    function setHistory(list) {
        state.history = Array.isArray(list) ? list : [];
        emit('history', state.history);
    }

    function addHistory(entry) {
        const list = state.history.slice();
        const sql = String(entry.sql || '').trim();
        if (!sql) return;
        for (let i = list.length - 1; i >= 0; i -= 1) {
            if (list[i].sql === sql) list.splice(i, 1);
        }
        list.unshift({
            sql: sql,
            at: entry.at || new Date().toISOString(),
            engine: entry.engine || 'sqlite',
            connection: entry.connection || '',
            rows: Number(entry.rows) || 0,
            elapsedMs: Number(entry.elapsedMs) || 0
        });
        state.history = list.slice(0, 60);
        emit('history', state.history);
    }

    const api = {
        data: state,
        on: on,
        emit: emit,
        addSession: addSession,
        removeSession: removeSession,
        session: session,
        activeSession: activeSession,
        setActiveSession: setActiveSession,
        sessionForProfile: sessionForProfile,
        addTab: addTab,
        tab: tab,
        activeTab: activeTab,
        setActiveTab: setActiveTab,
        updateTab: updateTab,
        removeTab: removeTab,
        tabsForSession: tabsForSession,
        setHistory: setHistory,
        addHistory: addHistory
    };

    // 让界面模块可以直接读写 state.xxx，同时保持 data 为唯一数据源。
    ['sessions', 'activeSessionId', 'profiles', 'tabs', 'activeTabId', 'history', 'settings', 'seq']
        .forEach(function (key) {
            Object.defineProperty(api, key, {
                enumerable: true,
                get: function () { return state[key]; },
                set: function (value) { state[key] = value; }
            });
        });

    window.SilkitState = api;
})();
