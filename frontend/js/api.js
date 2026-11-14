// IPC 客户端：把 `ipc.call` + `ipc.event` 包装成 Promise。
//
//   ipc.call  →  { ok: true, reqId }            立即返回，不阻塞 UI 线程
//   ipc.event →  { reqId, type, data|error }
//
// 标准事件：progress / result / error / cancelled。
// 业务事件（query.execute 的 statement / batch）通过 options.onEvent 透传。
//
// 用法：
//   const task = SilkitApi.queryExecute(sessionId, sql, {
//       onEvent: (type, data) => { if (type === 'batch') ...; }
//   });
//   task.promise.then(...).catch(...); task.cancel();
(function () {
    'use strict';

    const bridge = window.SilkitBridge;
    const pending = new Map();
    let seq = 0;

    // 保留后端 error 对象里的所有结构化字段（statementIndex/position/sql/offset...）。
    function makeError(detail, fallbackCode, fallbackMessage) {
        const data = detail && typeof detail === 'object' ? detail : {};
        const code = data.code || fallbackCode || 'unknown';
        const message = data.message || fallbackMessage || code;
        const error = new Error(message);
        error.code = code;
        error.details = data;
        if (data.sqlState) error.sqlState = data.sqlState;
        if (data.detail) error.detail = data.detail;
        if (data.hint) error.hint = data.hint;
        if (typeof data.position === 'number') error.position = data.position;
        if (typeof data.statementIndex === 'number') error.statementIndex = data.statementIndex;
        if (typeof data.offset === 'number') error.offset = data.offset;
        if (typeof data.elapsedMs === 'number') error.elapsedMs = data.elapsedMs;
        if (typeof data.sql === 'string') error.sql = data.sql;
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
            onEvent: typeof opts.onEvent === 'function' ? opts.onEvent : null,
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
                settle(reqId, entry.reject,
                    makeError({ code: 'timeout', message: command + ' 超时（' + opts.timeoutMs + ' ms）' }));
            }, opts.timeoutMs);
        }

        bridge.invoke('ipc.call', { cmd: command, reqId: reqId, args: args || {} })
            .then(function (ack) {
                if (!ack || ack.ok === true) return;
                const detail = ack.error || {};
                settle(reqId, entry.reject,
                    makeError(detail, 'rejected', detail.message || '请求被拒绝'));
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
            case 'error':
                settle(payload.reqId, entry.reject,
                    makeError(payload.error, 'error', 'IPC 执行失败'));
                break;
            case 'cancelled':
                settle(payload.reqId, entry.reject,
                    makeError({ code: 'cancelled', message: '已取消' }));
                break;
            default:
                // 业务事件：statement / batch / 未来的 progress 变体。
                if (entry.onEvent) {
                    try {
                        entry.onEvent(payload.type, payload.data || {}, payload);
                    } catch (error) {
                        console.error('[silkit] event handler ' + payload.type, error);
                    }
                }
                break;
        }
    });

    // 非请求型事件（例如 session.closed）转成 DOM 事件，视图层不必直接依赖桥。
    bridge.listen('session.closed', function (payload) {
        window.dispatchEvent(new CustomEvent('silkit:session-closed', { detail: payload || {} }));
    });

    // ---------------------------------------------------------------- 命令封装
    function listen(command, args, options) {
        return start(command, args, options);
    }

    window.SilkitApi = {
        start: start,
        call: call,
        listen: listen,
        pendingCount: function () { return pending.size; },

        info: function () { return call('app.info'); },
        ping: function (args, options) { return call('app.ping', args, options); },
        log: function (level, message) {
            return call('app.log', { level: level, message: message });
        },
        quit: function () { return call('app.quit'); },
        pickFile: function (args) { return call('app.pick_file', args || {}); },
        appReadFile: function (path) { return call('app.read_file', { path: path }); },
        appSaveFile: function (path, content) {
            return call('app.save_file', { path: path, content: content });
        },

        settingsGet: function () { return call('settings.get'); },
        settingsSet: function (patch) { return call('settings.set', { patch: patch }); },
        settingsReset: function () { return call('settings.reset'); },
        appPaths: function () { return call('app.paths'); },
        appOpenPath: function (kind) { return call('app.open_path', { kind: kind }); },

        themeList: function () { return call('theme.list'); },
        themeSet: function (theme, persist) {
            return call('theme.set', { theme: theme, persist: persist !== false });
        },

        // ------------------------------------------------------------ 连接
        connList: function () { return call('conn.list'); },
        connSave: function (profile, password, savePassword) {
            return call('conn.save', {
                profile: profile,
                password: password || '',
                savePassword: !!savePassword
            });
        },
        connDelete: function (id) { return call('conn.delete', { id: id }); },
        connTest: function (profile, password) {
            return call('conn.test', { profile: profile, password: password || '' });
        },
        connOpen: function (profile, password) {
            return call('conn.open', { profile: profile, password: password || '' });
        },
        connClose: function (sessionId) {
            return call('conn.close', { sessionId: sessionId });
        },
        connReconnect: function (sessionId, args) {
            return call('conn.reconnect', Object.assign({ sessionId: sessionId }, args || {}));
        },
        connPing: function (sessionId) {
            return call('conn.ping', { sessionId: sessionId });
        },
        connStats: function (sessionId) {
            return call('conn.stats', { sessionId: sessionId });
        },
        connSetDatabase: function (sessionId, database) {
            return call('conn.set_database', { sessionId: sessionId, database: database });
        },
        connSetReadonly: function (sessionId, readOnly) {
            return call('conn.set_readonly', { sessionId: sessionId, readOnly: !!readOnly });
        },

        // ------------------------------------------------------------ 元数据
        metaChildren: function (sessionId, parent) {
            return call('meta.children', { sessionId: sessionId, parent: parent || {} });
        },
        metaDetail: function (sessionId, ref) {
            return call('meta.detail', { sessionId: sessionId, ref: ref || {} });
        },
        metaDdl: function (sessionId, ref) {
            return call('meta.ddl', { sessionId: sessionId, ref: ref || {} });
        },
        metaRefresh: function (sessionId) {
            return call('meta.refresh', { sessionId: sessionId });
        },
        metaSearch: function (sessionId, keyword, options) {
            const opts = options || {};
            return call('meta.search', {
                sessionId: sessionId,
                keyword: keyword || '',
                kinds: opts.kinds || [],
                limit: opts.limit || 200
            });
        },

        // ------------------------------------------------------------ 查询
        queryExecute: function (sessionId, sql, options) {
            const opts = options || {};
            const args = { sessionId: sessionId, sql: sql };
            // 只把契约里允许的查询参数透传给后端，回调仍由 start() 消费。
            if (typeof opts.maxRows === 'number') args.maxRows = opts.maxRows;
            if (typeof opts.fetchSize === 'number') args.fetchSize = opts.fetchSize;
            if (typeof opts.maxCellBytes === 'number') args.maxCellBytes = opts.maxCellBytes;
            if (opts.continueOnError) args.continueOnError = true;
            if (opts.confirmDestructive && opts.confirmToken) {
                args.confirmDestructive = true;
                args.confirmToken = opts.confirmToken;
            }
            return start('query.execute', args, opts);
        },
        queryCancel: function (sessionId) {
            return call('query.cancel', { sessionId: sessionId });
        },
        queryExplain: function (sessionId, sql, options) {
            const opts = options || {};
            const args = { sessionId: sessionId, sql: sql };
            if (opts.analyze) args.analyze = true;
            return call('query.explain', args);
        },

        // ------------------------------------------------------------ 表数据（M5）
        tableRows: function (sessionId, ref, options) {
            const opts = options || {};
            const args = {
                sessionId: sessionId,
                ref: ref || {},
                page: Number(opts.page) || 0,
                pageSize: Number(opts.pageSize) || 500
            };
            if (opts.order && opts.order.column) args.order = opts.order;
            if (Array.isArray(opts.filters) && opts.filters.length) args.filters = opts.filters;
            if (opts.search) args.search = opts.search;
            return call('table.rows', args);
        },
        tableCount: function (sessionId, ref, options) {
            const opts = options || {};
            const args = {
                sessionId: sessionId,
                ref: ref || {},
                page: Number(opts.page) || 0,
                pageSize: Number(opts.pageSize) || 500
            };
            if (Array.isArray(opts.filters) && opts.filters.length) args.filters = opts.filters;
            if (opts.search) args.search = opts.search;
            return call('table.count', args);
        },
        tableApply: function (sessionId, ref, changes, dryRun) {
            return call('table.apply', {
                sessionId: sessionId,
                ref: ref || {},
                changes: changes || {},
                dryRun: !!dryRun
            });
        },
        tableCell: function (sessionId, ref, column, keys) {
            return call('table.cell', {
                sessionId: sessionId,
                ref: ref || {},
                column: column,
                keys: keys || {}
            });
        },
        tableTruncate: function (sessionId, ref, confirmName) {
            return call('table.truncate', {
                sessionId: sessionId,
                ref: ref || {},
                confirmName: confirmName
            });
        },

        // ------------------------------------------------------------ DDL 设计器（M6）
        ddlTypes: function (args) {
            return call('ddl.types', args || {});
        },
        ddlLoad: function (sessionId, ref) {
            return call('ddl.load', { sessionId: sessionId, ref: ref || {} });
        },
        ddlTables: function (sessionId, ref) {
            return call('ddl.tables', { sessionId: sessionId, ref: ref || {} });
        },
        ddlPreview: function (args) {
            return call('ddl.preview', args || {});
        },
        ddlApply: function (args) {
            return call('ddl.apply', args || {});
        },

        // ------------------------------------------------------------ 导入/导出/转储（M7）
        exportResult: function (args, options) {
            return start('export.result', args || {}, options);
        },
        importPreview: function (args) {
            return call('import.preview', args || {});
        },
        importCsv: function (args, options) {
            return start('import.csv', args || {}, options);
        },
        dumpSql: function (args, options) {
            return start('dump.sql', args || {}, options);
        },
        queryExecuteFile: function (args, options) {
            return start('query.execute_file', args || {}, options);
        }
    };
})();
