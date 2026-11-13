// 浏览器开发/测试环境的假后端：模拟 TauriCPP 的 invoke/listen 与异步事件。
// 真机运行时本文件不会被使用（bridge.js 优先原生桥）。
(function () {
    'use strict';

    const listeners = new Map();
    const timers = new Map();
    const SETTINGS_KEY = 'silkit.settings';

    const THEMES = [
        { id: 'bloom', name: '晨樱 Bloom', appearance: 'light', bg: '#FFF7F9', accent: '#C2255C' },
        { id: 'noir', name: '夜樱 Noir', appearance: 'dark', bg: '#17121A', accent: '#F06595' },
        { id: 'lilac', name: '薰衣草 Lilac', appearance: 'light', bg: '#FAF7FF', accent: '#6D4FC4' },
        { id: 'mocha', name: '奶咖 Mocha', appearance: 'light', bg: '#FBF7F3', accent: '#96563D' },
        { id: 'mint', name: '薄荷 Mint', appearance: 'light', bg: '#F5FBF9', accent: '#237668' }
    ];

    const DEFAULTS = {
        version: 1,
        ui: {
            theme: 'bloom', appearance: 'light', followSystem: false, reduceMotion: false,
            language: 'zh-CN', fontSize: 13, gridRowHeight: 26, sidebarWidth: 264,
            editorRatio: 0.44, restoreTabs: true, confirmDestructive: true
        },
        editor: { fontSize: 13, tabSize: 2, wordWrap: false, autocomplete: true, showLineNumbers: true },
        query: { maxRows: 5000, fetchSize: 500, timeoutMs: 0, autoLimit: true, historyLimit: 5000, safeMode: true },
        data: {
            pageSize: 500, nullDisplay: 'NULL', dateFormat: 'YYYY-MM-DD HH:mm:ss',
            binaryPreviewBytes: 65536, maxCellBytes: 262144
        },
        export: { defaultEncoding: 'utf-8', csvDelimiter: ',', includeHeader: true, nullLiteral: '' }
    };

    function clone(value) { return JSON.parse(JSON.stringify(value)); }

    function mergeJson(base, patch) {
        if (!patch || typeof patch !== 'object' || Array.isArray(patch) ||
            !base || typeof base !== 'object' || Array.isArray(base)) return clone(patch);
        Object.keys(patch).forEach(function (key) {
            if (base[key] && typeof base[key] === 'object' && !Array.isArray(base[key]) &&
                patch[key] && typeof patch[key] === 'object' && !Array.isArray(patch[key])) {
                base[key] = mergeJson(base[key], patch[key]);
            } else {
                base[key] = clone(patch[key]);
            }
        });
        return base;
    }

    function loadSettings() {
        try {
            const raw = localStorage.getItem(SETTINGS_KEY);
            if (raw) return mergeJson(clone(DEFAULTS), JSON.parse(raw));
        } catch (e) { /* ignore */ }
        return clone(DEFAULTS);
    }

    function saveSettings(settings) {
        try { localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings)); } catch (e) { /* ignore */ }
    }

    function emit(event, data) {
        const list = listeners.get(event) || [];
        list.slice().forEach(function (cb) {
            try { cb(data); } catch (e) { console.error('[mock] listener', e); }
        });
    }

    function listen(event, cb) {
        if (!listeners.has(event)) listeners.set(event, []);
        listeners.get(event).push(cb);
        return function () {
            listeners.set(event, (listeners.get(event) || []).filter(function (fn) { return fn !== cb; }));
        };
    }

    function later(fn, ms) { return setTimeout(fn, ms || 0); }

    function startTask(args) {
        const reqId = args.reqId;
        const cmd = args.cmd;
        const a = args.args || {};

        switch (cmd) {
            case 'app.info':
                later(function () {
                    emit('ipc.event', {
                        reqId: reqId, type: 'result', data: {
                            ok: true, name: 'Silkit', version: '0.0.1', build: 'browser mock',
                            platform: 'web', engines: ['sqlite', 'mysql', 'postgres'], dataDir: '(browser mock)'
                        }
                    });
                });
                return { ok: true, reqId };

            case 'app.ping': {
                const steps = Math.max(1, Math.min(10, Number(a.steps) || 3));
                const delay = Math.max(0, Math.min(2000, Number(a.delayMs) || 180));
                const started = Date.now();
                let step = 1;
                function tick() {
                    if (step <= steps) {
                        emit('ipc.event', {
                            reqId: reqId, type: 'progress',
                            data: { step: step, total: steps, message: '异步通道正常 ' + step }
                        });
                        step += 1;
                        timers.set(reqId, later(tick, delay));
                    } else {
                        timers.delete(reqId);
                        emit('ipc.event', {
                            reqId: reqId, type: 'result',
                            data: { pong: true, steps: steps, elapsedMs: Date.now() - started }
                        });
                    }
                }
                timers.set(reqId, later(tick, Math.min(120, delay)));
                return { ok: true, reqId };
            }

            case 'settings.get': {
                const settings = loadSettings();
                later(function () {
                    emit('ipc.event', { reqId: reqId, type: 'result', data: { settings: settings } });
                });
                return { ok: true, reqId };
            }

            case 'settings.set': {
                const settings = mergeJson(loadSettings(), a.patch || {});
                saveSettings(settings);
                later(function () {
                    emit('ipc.event', { reqId: reqId, type: 'result', data: { settings: settings } });
                });
                return { ok: true, reqId };
            }

            case 'theme.list':
                later(function () {
                    emit('ipc.event', { reqId: reqId, type: 'result', data: { themes: clone(THEMES) } });
                });
                return { ok: true, reqId };

            case 'theme.set': {
                const theme = THEMES.find(function (t) { return t.id === a.theme; }) || THEMES[0];
                if (a.persist !== false) {
                    const settings = loadSettings();
                    settings.ui.theme = theme.id;
                    settings.ui.appearance = theme.appearance;
                    settings.ui.followSystem = false;
                    saveSettings(settings);
                }
                later(function () {
                    emit('ipc.event', {
                        reqId: reqId, type: 'result',
                        data: { ok: true, theme: theme.id, windowBg: theme.bg }
                    });
                });
                return { ok: true, reqId };
            }

            case 'app.log':
                console.log('[silkit]', a.level || 'info', a.message || '');
                later(function () {
                    emit('ipc.event', { reqId: reqId, type: 'result', data: { ok: true } });
                });
                return { ok: true, reqId };

            case 'app.quit':
                window.close();
                return { ok: true, reqId };

            default:
                return { ok: false, error: { code: 'unknown_command', message: '未知命令: ' + cmd } };
        }
    }

    async function invoke(cmd, args) {
        if (cmd === 'ipc.call') return startTask(args || {});
        if (cmd === 'ipc.cancel') {
            const reqId = (args || {}).reqId;
            const timer = timers.get(reqId);
            const found = !!timer;
            if (timer) { clearTimeout(timer); timers.delete(reqId); }
            if (found) later(function () { emit('ipc.event', { reqId: reqId, type: 'cancelled' }); });
            return { ok: true, found: found };
        }
        return { ok: false, error: { code: 'unknown_command', message: '未知命令: ' + cmd } };
    }

    window.MockBridge = { invoke: invoke, listen: listen, emit: emit, isMock: true };
})();
