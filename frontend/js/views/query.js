// 查询执行：调用 query.execute，把 statement/batch 事件交给结果面板。
// M4：执行范围（选中 / 当前语句 / 全部）+ EXPLAIN（M4-3 / M4-10）。
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

        // scope: 'auto'（有选区用选区，否则全部）| 'all' | 'current'
        function sqlFor(tab, scope) {
            const mode = scope || 'auto';
            const tabs = ctx.tabs;
            if (tabs && state.activeTab() === tab && tabs.getSelectionText) {
                const selected = tabs.getSelectionText();
                if (selected && selected.trim()) {
                    return { sql: selected, label: '选中文本' };
                }
            }
            if (mode === 'current' && tabs && tabs.statementAtCursor) {
                const statement = tabs.statementAtCursor();
                if (statement && statement.sql && statement.sql.trim()) {
                    return { sql: statement.sql, label: '当前语句' };
                }
                toast('光标附近没有可执行的语句', 'error');
                return null;
            }
            return { sql: tab.sql || '', label: '全部语句' };
        }

        // 安全模式：后端返回 confirm_required 时展示危险语句并二次确认。
        async function confirmDanger(details) {
            const info = details || {};
            const statements = Array.isArray(info.statements) ? info.statements : [];
            const I = window.SilkitI18n;
            const title = I ? I.t('danger.title') : '确认执行危险语句';
            const summary = I
                ? I.t('danger.message', { n: info.count || statements.length })
                : '安全模式检测到 ' + (info.count || statements.length) + ' 条危险语句，执行后可能无法撤销。';
            const preview = statements.map(function (item) {
                return '#' + (Number(item.index) + 1) + '  ' +
                    String(item.sql || '').replace(/\s+/g, ' ').slice(0, 120);
            }).join('\n');
            return window.SilkitUtil.confirm({
                title: title,
                message: summary,
                hint: '这些语句会直接作用于当前数据库，可在「设置」中关闭安全模式。',
                preview: preview,
                okText: '仍然执行',
                danger: true
            });
        }

        async function run(tab, scope, confirm) {
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

            const picked = sqlFor(tab, scope);
            if (!picked) return;
            const sql = String(picked.sql || '').trim();
            if (!sql) {
                toast('SQL 不能为空', 'error');
                return;
            }

            tab.runSql = sql;
            tab.runLabel = picked.label;
            results.begin(tab);
            setRunning(tab, true);
            if (picked.label !== '全部语句') {
                results.addMessage(tab, 'info', '执行范围：' + picked.label);
            }
            if (ctx.tabs && ctx.tabs.clearErrorHighlight) ctx.tabs.clearErrorHighlight();

            const task = api.queryExecute(session.id, sql, {
                onEvent: function (type, data) {
                    if (type === 'statement') results.statement(tab, data);
                    else if (type === 'batch') results.batch(tab, data);
                },
                onProgress: function (data) {
                    if (data && data.message) {
                        results.addMessage(tab, 'progress', data.message);
                    }
                },
                confirmDestructive: !!(confirm && confirm.confirmDestructive),
                confirmToken: confirm ? confirm.confirmToken : undefined
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
                } else if (error && error.code === 'confirm_required') {
                    tab.task = null;
                    setRunning(tab, false);
                    if (await confirmDanger(error.details)) {
                        return run(tab, scope, {
                            confirmDestructive: true,
                            confirmToken: error.details && error.details.token
                        });
                    }
                    results.addMessage(tab, 'info', '已取消：危险语句未执行');
                    results.showMessages();
                    return;
                } else {
                    results.fail(tab, error || { message: '执行失败' }, sql);
                    toast('执行失败：' + ((error && error.message) || '未知错误'), 'error');
                }
            } finally {
                tab.task = null;
                setRunning(tab, false);
            }
        }

        async function explain(tab) {
            if (!tab) return;
            if (tab.running) {
                toast('正在执行中，请等待完成或取消', 'error');
                return;
            }
            const session = resolveSession(tab);
            if (!session) {
                toast('请先打开一个数据库连接', 'error');
                return;
            }
            const picked = sqlFor(tab, 'current');
            if (!picked) return;
            const sql = String(picked.sql || '').trim();
            if (!sql) {
                toast('SQL 不能为空', 'error');
                return;
            }
            try {
                results.addMessage(tab, 'info', '生成执行计划（' + picked.label + '）…');
                const data = await api.queryExplain(session.id, sql);
                results.showExplain(tab, data || {});
            } catch (error) {
                const message = (error && error.message) || String(error);
                results.addMessage(tab, 'error', 'EXPLAIN 失败：' + message);
                results.showMessages();
                toast('EXPLAIN 失败：' + message, 'error');
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

        return { run: run, cancel: cancel, explain: explain };
    }

    window.SilkitQuery = { create: create };
})();
