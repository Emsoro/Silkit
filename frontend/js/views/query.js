// 查询执行：调用 query.execute，把 statement/batch 事件交给结果面板。
(function () {
    'use strict';

    function create(ctx) {
        const api = ctx.api;
        const state = ctx.state;
        const toast = ctx.toast;
        const results = ctx.results;

        function resolveSession(tab) {
            let session = tab.sessionId ? state.session(tab.sessionId) : null;
            if (!session) session = state.activeSession();
            if (session && !tab.sessionId) tab.sessionId = session.id;
            return session;
        }

        function sqlFor(tab) {
            if (state.activeTab() === tab && ctx.getSelectionText) {
                const selected = ctx.getSelectionText();
                if (selected && selected.trim()) return selected;
            }
            return tab.sql || '';
        }

        async function run(tab) {
            if (!tab) return;
            if (tab.running) {
                cancel(tab);
                return;
            }

            const session = resolveSession(tab);
            if (!session) {
                toast('请先打开一个数据库连接', 'error');
                if (ctx.focusConnections) ctx.focusConnections();
                return;
            }
            if (state.activeSessionId !== session.id) state.setActiveSession(session.id);

            const sql = String(sqlFor(tab) || '').trim();
            if (!sql) {
                toast('SQL 不能为空', 'error');
                return;
            }

            tab.runSql = sql;
            results.begin(tab);
            setRunning(tab, true);

            const task = api.queryExecute(session.id, sql, {
                onEvent: function (type, data) {
                    if (type === 'statement') results.statement(tab, data);
                    else if (type === 'batch') results.batch(tab, data);
                },
                onProgress: function (data) {
                    if (data && data.message) {
                        results.addMessage(tab, 'progress', data.message);
                    }
                }
            });
            tab.task = task;

            try {
                const result = await task.promise;
                if (result && result.cancelled) {
                    results.cancelled(tab);
                } else {
                    results.finish(tab, result || {});
                    if (ctx.onExecuted) {
                        ctx.onExecuted(sql, session, result || {});
                    }
                }
            } catch (error) {
                if (error && error.code === 'cancelled') {
                    results.cancelled(tab);
                } else {
                    results.fail(tab, error || { message: '执行失败' }, sql);
                    toast('执行失败：' + ((error && error.message) || '未知错误'), 'error');
                }
            } finally {
                tab.task = null;
                setRunning(tab, false);
            }
        }

        function setRunning(tab, running) {
            tab.running = !!running;
            if (ctx.onRunningChanged) ctx.onRunningChanged(tab, !!running);
        }

        function cancel(tab) {
            const target = tab || state.activeTab();
            if (!target || !target.task) {
                if (target && target.sessionId) {
                    api.queryCancel(target.sessionId).catch(function () { /* ignore */ });
                }
                return;
            }
            target.task.cancel();
            toast('正在取消查询…');
        }

        return { run: run, cancel: cancel };
    }

    window.SilkitQuery = { create: create };
})();
