// 结果面板：流式批次合并、多结果集切换、消息日志、错误定位、单元格查看器。
(function () {
    'use strict';

    const U = window.SilkitUtil;

    function create(ctx) {
        const api = ctx.api;
        const state = ctx.state;
        const toast = ctx.toast;

        const ids = {
            grid: 'resultGrid',
            logWrap: 'messageLog',
            logList: 'logList',
            logEmpty: 'logEmpty',
            msgCount: 'msgCount',
            meta: 'resultMeta',
            setSelect: 'resultSetSelect',
            copyBtn: 'resultCopyBtn',
            error: 'resultError',
            errorText: 'resultErrorText',
            errorPos: 'resultErrorPos',
            footRows: 'resultFootRows',
            footState: 'resultFootState',
            footTrunc: 'resultFootTrunc',
            footEncoding: 'resultFootEncoding'
        };

        const dom = {};
        let grid = null;
        let currentView = 'grid';
        let cell = null;

        function byId(id) { return document.getElementById(id); }

        function init() {
            Object.keys(ids).forEach(function (key) { dom[key] = byId(ids[key]); });

            grid = window.SilkitGrid.create(dom.grid, {
                onOpenCell: openCell,
                onToast: function (message, variant) { toast(message, variant); }
            });

            document.querySelectorAll('.rtab').forEach(function (tab) {
                tab.addEventListener('click', function () {
                    showView(tab.getAttribute('data-target') === ids.logWrap ? 'messages' : 'grid');
                });
            });

            if (dom.setSelect) {
                dom.setSelect.addEventListener('change', function () {
                    const tab = state.activeTab();
                    if (!tab || !tab.result) return;
                    const index = Number(dom.setSelect.value);
                    const set = tab.result.sets[index];
                    if (!set) return;
                    tab.result.displaySet = set;
                    displaySet(tab, set);
                    updateMeta(tab);
                });
            }

            if (dom.copyBtn) {
                dom.copyBtn.addEventListener('click', function () {
                    if (grid) grid.copyAll(true);
                });
            }

            bindCellViewer();
        }

        // ------------------------------------------------------------ 标签状态
        function ensureResult(tab) {
            if (!tab.result) {
                tab.result = {
                    sets: [],
                    current: null,
                    displaySet: null,
                    elapsedMs: 0,
                    rows: 0,
                    affected: 0,
                    truncated: false,
                    statements: [],
                    error: null,
                    state: 'idle',
                    view: 'grid',
                    signature: ''
                };
            }
            return tab.result;
        }

        function isActive(tab) {
            return state.activeTab() === tab;
        }

        function columnsSignature(columns) {
            return (columns || []).map(function (column) {
                return (column.name || '') + '|' + (column.type || '') + '|' + (column.kind || '');
            }).join('\u0001');
        }

        function setSelector(tab) {
            if (!dom.setSelect) return;
            const result = tab.result;
            if (!result || result.sets.length <= 1) {
                dom.setSelect.hidden = true;
                U.clear(dom.setSelect);
                return;
            }
            U.clear(dom.setSelect);
            result.sets.forEach(function (set, index) {
                const option = document.createElement('option');
                option.value = String(index);
                option.textContent = '结果 ' + (index + 1) + ' · ' + U.formatNumber(set.rows.length) + ' 行';
                dom.setSelect.appendChild(option);
            });
            const active = result.displaySet || result.sets[result.sets.length - 1];
            const index = result.sets.indexOf(active);
            dom.setSelect.value = String(index >= 0 ? index : 0);
            dom.setSelect.hidden = false;
        }

        function setError(tab) {
            if (!dom.error) return;
            const result = tab.result;
            if (!result || !result.error) {
                dom.error.hidden = true;
                return;
            }
            const error = result.error;
            dom.error.hidden = false;
            dom.errorText.textContent = error.message || String(error);
            let position = '';
            if (typeof error.offset === 'number' && tab.runSql) {
                const lc = U.lineColumnAt(tab.runSql, error.offset);
                position = '第 ' + lc.line + ' 行第 ' + lc.column + ' 列';
            } else if (typeof error.position === 'number' && error.position > 0) {
                position = '语句内第 ' + error.position + ' 个字符';
            }
            if (typeof error.statementIndex === 'number') {
                position = (position ? position + ' · ' : '') + '语句 ' + (error.statementIndex + 1);
            }
            dom.errorPos.textContent = position;
        }

        function updateMeta(tab) {
            if (!isActive(tab)) return;
            const result = tab.result;
            if (!result) {
                if (dom.meta) dom.meta.textContent = '';
                return;
            }
            let text = '';
            if (result.state === 'running') {
                text = '执行中' + (result.current ? ' · ' + U.formatNumber(result.rows) + ' 行' : '…');
            } else if (result.state === 'error') {
                text = '失败 · ' + (result.error && result.error.code ? result.error.code : 'unknown');
            } else if (result.state === 'cancelled') {
                text = '已取消';
            } else if (result.state === 'done') {
                if (result.rows > 0) {
                    text = U.formatNumber(result.rows) + ' 行 · ' + U.formatDuration(result.elapsedMs);
                } else if (result.affected > 0) {
                    text = '影响 ' + U.formatNumber(result.affected) + ' 行 · ' +
                        U.formatDuration(result.elapsedMs);
                } else {
                    text = '0 行 · ' + U.formatDuration(result.elapsedMs);
                }
                if (result.truncated) text += ' · 已截断';
            }
            if (dom.meta) dom.meta.textContent = text;

            if (dom.footRows) {
                if (result.rows > 0) {
                    dom.footRows.textContent = U.formatNumber(result.rows) + ' 行';
                } else if (result.affected > 0) {
                    dom.footRows.textContent = '影响 ' + U.formatNumber(result.affected) + ' 行';
                } else {
                    dom.footRows.textContent = '0 行';
                }
            }
            if (dom.footState) {
                dom.footState.textContent = result.state === 'running' ? '执行中' :
                    (result.state === 'error' ? '失败' :
                        (result.state === 'cancelled' ? '已取消' : '就绪'));
            }
            if (dom.footTrunc) {
                dom.footTrunc.hidden = !result.truncated;
                dom.footTrunc.textContent = result.truncated ? '结果已截断' : '';
            }
            if (dom.footEncoding) dom.footEncoding.textContent = 'UTF-8';
            if (dom.copyBtn) dom.copyBtn.disabled = !(result.displaySet && result.displaySet.rows.length);
            setSelector(tab);
            setError(tab);
        }

        function displaySet(tab, set) {
            if (!isActive(tab)) return;
            if (!set || !set.rows.length && !(set.columns || []).length) {
                grid.setEmpty(tab.result && tab.result.message ? tab.result.message : '还没有结果',
                    tab.result && tab.result.hint ? tab.result.hint : '按 Ctrl+Enter 执行当前 SQL');
                return;
            }
            grid.setSet(set);
        }

        // ------------------------------------------------------------ 结果视图
        function showView(view) {
            currentView = view;
            const tab = state.activeTab();
            if (tab) {
                ensureResult(tab).view = view;
            }
            document.querySelectorAll('.rtab').forEach(function (button) {
                const target = button.getAttribute('data-target');
                const active = (view === 'messages' && target === ids.logWrap) ||
                    (view === 'grid' && target === ids.grid);
                button.classList.toggle('active', active);
                button.setAttribute('aria-selected', active ? 'true' : 'false');
            });
            if (dom.grid) dom.grid.hidden = view !== 'grid';
            if (dom.logWrap) dom.logWrap.hidden = view !== 'messages';
        }

        function showMessages() { showView('messages'); }

        // ------------------------------------------------------------ 消息
        function addMessage(tab, level, text) {
            if (!tab) return;
            if (!Array.isArray(tab.messages)) tab.messages = [];
            tab.messages.push({ level: level || 'info', text: String(text), at: new Date().toISOString() });
            if (tab.messages.length > 500) tab.messages.splice(0, tab.messages.length - 500);
            if (isActive(tab)) renderMessages(tab);
        }

        function renderMessages(tab) {
            if (!dom.logList) return;
            const messages = tab && Array.isArray(tab.messages) ? tab.messages : [];
            U.clear(dom.logList);
            if (dom.logEmpty) dom.logEmpty.hidden = messages.length > 0;
            messages.forEach(function (message) {
                dom.logList.appendChild(messageNode(message));
            });
            if (dom.msgCount) {
                dom.msgCount.hidden = messages.length === 0;
                dom.msgCount.textContent = String(messages.length);
            }
            dom.logList.scrollTop = dom.logList.scrollHeight;
        }

        function messageNode(message) {
            const line = U.el('li', 'log-line');
            line.setAttribute('data-level', message.level || 'info');
            const time = U.el('span', 'log-time', U.clockTime(new Date(message.at)));
            const tag = U.el('span', 'log-tag', message.level || 'info');
            const body = U.el('span', 'log-text', message.text);
            line.appendChild(time);
            line.appendChild(tag);
            line.appendChild(body);
            return line;
        }

        // ------------------------------------------------------------ 执行生命周期
        function begin(tab) {
            const result = ensureResult(tab);
            result.sets = [];
            result.current = null;
            result.displaySet = null;
            result.elapsedMs = 0;
            result.rows = 0;
            result.affected = 0;
            result.truncated = false;
            result.statements = [];
            result.error = null;
            result.state = 'running';
            result.message = '';
            result.hint = '';
            result.view = 'grid';
            tab.messages = [];
            if (isActive(tab)) {
                grid.setEmpty('正在执行…', '结果会边执行边显示');
                showView('grid');
                updateMeta(tab);
                renderMessages(tab);
            }
        }

        function statement(tab, data) {
            if (!isActive(tab)) return;
            if (dom.meta) {
                dom.meta.textContent = '执行语句 ' + (Number(data.index) + 1) + '/' + Number(data.total) + '…';
            }
        }

        function batch(tab, data) {
            const result = tab.result;
            if (!result) return;
            const rows = Array.isArray(data.rows) ? data.rows : [];
            const incoming = Array.isArray(data.columns) ? data.columns : null;
            let set = result.current;

            if (incoming && incoming.length) {
                const signature = columnsSignature(incoming);
                if (!set || set.statement !== data.statement || set.signature !== signature) {
                    set = {
                        statement: Number(data.statement) || 0,
                        columns: incoming,
                        rows: [],
                        signature: signature,
                        elapsedMs: 0,
                        affected: 0,
                        truncated: false
                    };
                    result.sets.push(set);
                    result.current = set;
                    result.displaySet = set;
                }
            }

            if (!set && rows.length) {
                const width = rows.reduce(function (max, row) {
                    return Math.max(max, Array.isArray(row) ? row.length : 0);
                }, 0);
                const columns = [];
                for (let i = 0; i < width; i += 1) {
                    columns.push({ name: '列 ' + (i + 1), type: '', kind: '' });
                }
                set = {
                    statement: Number(data.statement) || 0,
                    columns: columns,
                    rows: [],
                    signature: 'synthetic:' + width,
                    elapsedMs: 0,
                    affected: 0,
                    truncated: false
                };
                result.sets.push(set);
                result.current = set;
                result.displaySet = set;
            }

            if (!set) return;
            if (rows.length) {
                Array.prototype.push.apply(set.rows, rows);
                result.rows += rows.length;
            }

            if (isActive(tab)) {
                if (result.displaySet === set) {
                    if (grid.rowCount() === 0) grid.setSet(set);
                    else grid.refresh();
                }
                updateMeta(tab);
            }
        }

        function finish(tab, data) {
            const result = ensureResult(tab);
            result.state = 'done';
            result.elapsedMs = Number(data.elapsedMs) || 0;
            result.statements = Array.isArray(data.statements) ? data.statements : [];
            result.rows = 0;
            result.affected = 0;
            result.truncated = false;

            result.statements.forEach(function (summary) {
                result.rows += Number(summary.rows) || 0;
                result.affected += Number(summary.affected) || 0;
                result.truncated = result.truncated || !!summary.truncated;
                if (result.current && Number(summary.index) === result.current.statement) {
                    result.current.elapsedMs = Number(summary.elapsedMs) || 0;
                    result.current.affected = Number(summary.affected) || 0;
                    result.current.truncated = !!summary.truncated;
                }
            });

            if (!result.sets.length) {
                if (result.affected > 0) {
                    result.message = '执行成功';
                    result.hint = '影响 ' + U.formatNumber(result.affected) + ' 行';
                } else if (result.statements.length > 1) {
                    result.message = '执行成功';
                    result.hint = U.formatNumber(result.statements.length) + ' 条语句已完成';
                } else {
                    result.message = '执行成功';
                    result.hint = '没有返回结果集';
                }
            }

            if (!result.displaySet && result.sets.length) {
                result.displaySet = result.sets[result.sets.length - 1];
            }

            result.statements.forEach(function (summary, index) {
                const ms = U.formatDuration(summary.elapsedMs);
                if (Number(summary.rows) > 0) {
                    addMessage(tab, 'result', '语句 ' + (index + 1) + ' · ' +
                        U.formatNumber(summary.rows) + ' 行 · ' + ms);
                } else if (Number(summary.affected) > 0) {
                    addMessage(tab, 'result', '语句 ' + (index + 1) + ' · 影响 ' +
                        U.formatNumber(summary.affected) + ' 行 · ' + ms);
                } else {
                    addMessage(tab, 'result', '语句 ' + (index + 1) + ' · 完成 · ' + ms);
                }
            });
            if (result.truncated) addMessage(tab, 'info', '达到行数上限，结果已截断。');
            addMessage(tab, 'result', '执行完成 · ' + U.formatDuration(result.elapsedMs));

            if (isActive(tab)) {
                if (result.displaySet) {
                    grid.setSet(result.displaySet);
                } else {
                    grid.setEmpty(result.message || '执行成功', result.hint || '没有返回结果集');
                }
                updateMeta(tab);
                renderMessages(tab);
            }
        }

        function fail(tab, error, sqlText) {
            const result = ensureResult(tab);
            result.state = 'error';
            result.error = error;
            result.elapsedMs = Number(error.elapsedMs) || 0;
            tab.runSql = sqlText || tab.runSql || '';

            const position = (typeof error.offset === 'number')
                ? U.lineColumnAt(tab.runSql, error.offset)
                : null;
            let text = error.message || String(error);
            if (error.detail) text += ' · ' + error.detail;
            if (error.hint) text += ' · ' + error.hint;
            addMessage(tab, 'error', text +
                (position ? '（第 ' + position.line + ' 行第 ' + position.column + ' 列）' : ''));

            if (isActive(tab)) {
                if (!result.rows) {
                    grid.setEmpty('执行失败', text);
                    showView('messages');
                }
                updateMeta(tab);
                renderMessages(tab);
                if (position && ctx.onErrorPosition) ctx.onErrorPosition(position, error);
            }
        }

        function cancelled(tab) {
            const result = ensureResult(tab);
            result.state = 'cancelled';
            addMessage(tab, 'error', '查询已取消，连接仍然可用。');
            if (isActive(tab)) {
                if (!result.rows) grid.setEmpty('已取消', '连接仍可继续查询');
                updateMeta(tab);
                renderMessages(tab);
            }
        }

        function show(tab) {
            if (!tab) {
                if (grid) grid.setEmpty('还没有结果', '打开连接并执行 SQL');
                return;
            }
            const result = ensureResult(tab);
            currentView = result.view || 'grid';
            displaySet(tab, result.displaySet);
            updateMeta(tab);
            renderMessages(tab);
            showView(currentView);
        }

        // ------------------------------------------------------------ 单元格查看器
        function bindCellViewer() {
            const dialog = byId('cellViewer');
            if (!dialog) return;
            const close = byId('cellViewerClose');
            const copy = byId('cellViewerCopy');
            if (close) close.addEventListener('click', function () { dialog.hidden = true; });
            dialog.addEventListener('click', function (event) {
                if (event.target === dialog) dialog.hidden = true;
            });
            if (copy) {
                copy.addEventListener('click', function () {
                    if (!cell) return;
                    const text = cell.value == null ? '' : String(cell.value);
                    U.copyText(text).then(function () {
                        toast('单元格内容已复制');
                    }).catch(function (error) {
                        toast('复制失败：' + (error.message || error), 'error');
                    });
                });
            }
            document.addEventListener('keydown', function (event) {
                if (event.key === 'Escape' && !dialog.hidden) dialog.hidden = true;
            });
        }

        function openCell(info) {
            cell = info;
            const dialog = byId('cellViewer');
            if (!dialog) return;
            const title = byId('cellViewerTitle');
            const meta = byId('cellViewerMeta');
            const body = byId('cellViewerBody');
            const raw = info.value;
            const column = info.column || {};
            const kind = String(column.kind || '').toLowerCase();

            title.textContent = (column.name || '单元格') + ' · 第 ' + (info.rowIndex + 1) + ' 行';
            const parts = [];
            if (column.type) parts.push(column.type);
            else if (column.kind) parts.push(column.kind);
            if (raw == null) parts.push('NULL');
            else if (kind === 'binary') parts.push(U.formatBytes(U.base64Bytes(raw)) + ' · Base64');
            else parts.push(U.formatNumber(String(raw).length) + ' 字符');
            meta.textContent = parts.join(' · ');

            U.clear(body);
            if (raw == null) {
                body.appendChild(U.el('div', 'cell-viewer-null', 'NULL'));
            } else if (kind === 'binary') {
                const base64 = U.el('pre', 'cell-viewer-pre', String(raw));
                body.appendChild(U.el('div', 'cell-viewer-label', 'Base64'));
                body.appendChild(base64);
                const hex = U.hexPreview(String(raw), 256);
                if (hex) {
                    body.appendChild(U.el('div', 'cell-viewer-label', '十六进制（前 256 字节）'));
                    body.appendChild(U.el('pre', 'cell-viewer-pre cell-viewer-hex', hex));
                }
            } else {
                body.appendChild(U.el('pre', 'cell-viewer-pre', String(raw)));
            }

            dialog.hidden = false;
            const close = byId('cellViewerClose');
            if (close) close.focus();
        }

        return {
            init: init,
            begin: begin,
            statement: statement,
            batch: batch,
            finish: finish,
            fail: fail,
            cancelled: cancelled,
            show: show,
            showView: showView,
            showMessages: showMessages,
            addMessage: addMessage,
            renderMessages: renderMessages,
            openCell: openCell,
            copyAll: function () { if (grid) grid.copyAll(true); }
        };
    }

    window.SilkitResults = { create: create };
})();
