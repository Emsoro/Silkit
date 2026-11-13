// IPC 客户端：把 `ipc.call` + `ipc.event` 包装成 Promise。
//
//   ipc.call  →  { ok: true, reqId }            立即返回，不阻塞 UI 线程
//   ipc.event →  { reqId, type: progress|result|error|cancelled, data|error }
//
// 用法：
//   const task = SilkitApi.start('app.ping', { steps: 3 }, { onProgress });
//   task.promise.then(...).catch(...); task.cancel();
//   SilkitApi.call('settings.get').then(...)
(function () {
    'use strict';

    const bridge = window.SilkitBridge;
    const pending = new Map();
    let seq = 0;

    function Error_(code, message) {
        const error = new Error(message || code);
        error.code = code || 'unknown';
        return error;
    }

    function nextReqId() {
        seq += 1;
        return 'r' + Date.now().toString(36) + '-' + seq.toString(36);
    }

    function settle(reqId, fn, value) {
        const entry = pending.get(reqId);
        if (!entry || entry.settled) return;
        entry.settled = true;
        if (entry.timer) clearTimeout(entry.timer);
        pending.delete(reqId);
        fn(value);
    }

    function start(command, args, options) {
        const opts = options || {};
        const reqId = nextReqId();

        const entry = {
            reqId: reqId,
            command: command,
            settled: false,
            timer: 0,
            onProgress: typeof opts.onProgress === 'function' ? opts.onProgress : null,
            resolve: null,
            reject: null
        };

        const promise = new Promise(function (resolve, reject) {
            entry.resolve = resolve;
            entry.reject = reject;
        });

        pending.set(reqId, entry);

        if (opts.timeoutMs && opts.timeoutMs > 0) {
            entry.timer = setTimeout(function () {
                settle(reqId, entry.reject, Error_('timeout', command + ' 超时（' + opts.timeoutMs + ' ms）'));
            }, opts.timeoutMs);
        }

        bridge.invoke('ipc.call', { cmd: command, reqId: reqId, args: args || {} })
            .then(function (ack) {
                if (!ack || ack.ok === true) return;
                const detail = ack.error || {};
                settle(reqId, entry.reject, Error_(detail.code || 'rejected', detail.message || '请求被拒绝'));
            })
            .catch(function (error) {
                settle(reqId, entry.reject, error);
            });

        return {
            reqId: reqId,
            command: command,
            promise: promise,
            cancel: function () {
                if (entry.settled) return Promise.resolve(false);
                return bridge.invoke('ipc.cancel', { reqId: reqId })
                    .then(function (result) { return !!(result && result.found); })
                    .catch(function () { return false; });
            }
        };
    }

    function call(command, args, options) {
        return start(command, args, options).promise;
    }

    bridge.listen('ipc.event', function (payload) {
        if (!payload || !payload.reqId) return;
        const entry = pending.get(payload.reqId);
        if (!entry) return;

        switch (payload.type) {
            case 'progress':
                if (entry.onProgress) {
                    try {
                        entry.onProgress(payload.data || {});
                    } catch (error) {
                        console.error('[silkit] progress handler', error);
                    }
                }
                break;
            case 'result':
                settle(payload.reqId, entry.resolve, payload.data);
                break;
            case 'error': {
                const detail = payload.error || {};
                settle(payload.reqId, entry.reject,
                    Error_(detail.code || 'error', detail.message || 'IPC 执行失败'));
                break;
            }
            case 'cancelled':
                settle(payload.reqId, entry.reject, Error_('cancelled', '已取消'));
                break;
            default:
                break;
        }
    });

    window.SilkitApi = {
        start: start,
        call: call,
        pendingCount: function () { return pending.size; },

        info: function () { return call('app.info'); },
        ping: function (args, options) { return call('app.ping', args, options); },
        log: function (level, message) {
            return call('app.log', { level: level, message: message });
        },
        quit: function () { return call('app.quit'); },

        settingsGet: function () { return call('settings.get'); },
        settingsSet: function (patch) { return call('settings.set', { patch: patch }); },

        themeList: function () { return call('theme.list'); },
        themeSet: function (theme, persist) {
            return call('theme.set', { theme: theme, persist: persist !== false });
        }
    };
})();
