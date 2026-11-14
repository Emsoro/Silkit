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
            explain: 'explainView',
            explainPre: 'explainPre',
            explainWarn: 'explainWarn',
            explainEmpty: 'explainEmpty',
            logWrap: 'messageLog',
            logList: 'logList',
            logEmpty: 'logEmpty',
            msgCount: 'msgCount',
            meta: 'resultMeta',
            sets: 'resultSets',
            copyBtn: 'resultCopyBtn',
            exportCsvBtn: 'exportCsvBtn',
            exportJsonBtn: 'exportJsonBtn',
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
                    const target = tab.getAttribute('data-target');
                    showView(target === ids.logWrap ? 'messages' :
                        (target === ids.explain ? 'explain' : 'grid'));
                });
            });

            if (dom.sets) {
                dom.sets.addEventListener('click', function (event) {
                    const node = event.target.closest('.rset');
                    if (!node) return;
                    const tab = state.activeTab();
                    if (!tab || !tab.result) return;
                    const index = Number(node.getAttribute('data-set'));
                    const set = tab.result.sets[index];
                    if (!set) return;
                    tab.result.displaySet = set;
                    renderSetTabs(tab);
                    displaySet(tab, set);
                    updateMeta(tab);
                });
            }

            if (dom.copyBtn) {
                dom.copyBtn.addEventListener('click', function () {
                    if (grid) grid.copyAll(true);
                });
            }

            if (dom.exportCsvBtn) {
                dom.exportCsvBtn.addEventListener('click', function () { exportResult('csv'); });
            }
            if (dom.exportJsonBtn) {
                dom.exportJsonBtn.addEventListener('click', function () { exportResult('json'); });
            }

            bindCellViewer();
        }

        // ------------------------------------------------------------ 导出（M7 向导）
        function exportResult(format) {
            const tab = state.activeTab();
            const set = tab && tab.result ? tab.result.displaySet : null;
            if (!set || !set.columns || !set.columns.length) {
                toast('当前没有可导出的结果集', 'error');
                return;
            }
            if (!ctx.io) {
                toast('导出向导不可用', 'error');
                return;
            }
            ctx.io.openExport({
                label: '当前结果集 · ' + U.formatNumber(set.rows.length) + ' 行',
                format: format,
                sql: (tab.runSql || '').trim(),
                pathHint: 'result-' + new Date().toISOString().replace(/[:.]/g, '-')
            });
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

        function renderSetTabs(tab) {
            if (!dom.sets) return;
            const result = tab.result;
            U.clear(dom.sets);
            if (!result || result.sets.length <= 1) return;
            const active = result.displaySet || result.sets[result.sets.length - 1];
            result.sets.forEach(function (set, index) {
                const node = document.createElement('button');
                node.type = 'button';
                node.className = 'rset' + (set === active ? ' active' : '');
                node.setAttribute('data-set', String(index));
                node.setAttribute('role', 'tab');
                const label = '结果 ' + (index + 1);
                node.appendChild(U.el('span', 'rset-label', label));
                node.appendChild(U.el('span', 'rset-count', U.formatNumber(set.rows.length)));
                node.title = label + ' · ' + U.formatNumber(set.rows.length) + ' 行';
                dom.sets.appendChild(node);
            });
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
            const hasRows = !!(result.displaySet && result.displaySet.rows.length);
            if (dom.copyBtn) dom.copyBtn.disabled = !hasRows;
            if (dom.exportCsvBtn) dom.exportCsvBtn.disabled = !hasRows;
            if (dom.exportJsonBtn) dom.exportJsonBtn.disabled = !hasRows;
            renderSetTabs(tab);
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
                    (view === 'explain' && target === ids.explain) ||
                    (view === 'grid' && target === ids.grid);
                button.classList.toggle('active', active);
                button.setAttribute('aria-selected', active ? 'true' : 'false');
            });
            if (dom.grid) dom.grid.hidden = view !== 'grid';
            if (dom.explain) dom.explain.hidden = view !== 'explain';
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

        // ------------------------------------------------------------ 执行计划（M4-10）
        function showExplain(tab, data) {
            const result = ensureResult(tab);
            result.explain = data || {};
            if (!isActive(tab)) return;
            const rows = Array.isArray(data && data.plan) ? data.plan : [];
            const lines = [];
            rows.forEach(function (row) {
                if (Array.isArray(row)) {
                    Array.prototype.push.apply(lines,
                        row.join(' | ').split('\n').filter(function (line) { return line.trim(); }));
                } else {
                    lines.push(String(row));
                }
            });
            const text = lines.join('\n') ||
                (data && data.message) || '（没有返回计划行）';
            dom.explainPre.textContent = text;
            dom.explainPre.hidden = false;
            if (dom.explainEmpty) dom.explainEmpty.hidden = true;
            if (dom.explainWarn) {
                const warnings = Array.isArray(data && data.warnings) ? data.warnings : [];
                dom.explainWarn.textContent = warnings.join(' · ');
                dom.explainWarn.hidden = !warnings.length;
            }
            showView('explain');
            addMessage(tab, 'result', '执行计划已生成' +
                (data && data.engine ? '（' + data.engine + '）' : '') + ' · ' + lines.length + ' 行');
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
        function base64ToBytes(text) {
            try {
                const binary = atob(String(text || ''));
                const bytes = new Uint8Array(binary.length);
                for (let i = 0; i < binary.length; i += 1) {
                    bytes[i] = binary.charCodeAt(i);
                }
                return bytes;
            } catch (error) {
                return new Uint8Array(0);
            }
        }

        function imageMimeOf(bytes) {
            if (!bytes || bytes.length < 4) return '';
            if (bytes[0] === 0x89 && bytes[1] === 0x50 && bytes[2] === 0x4E && bytes[3] === 0x47) {
                return 'image/png';
            }
            if (bytes[0] === 0xFF && bytes[1] === 0xD8 && bytes[2] === 0xFF) return 'image/jpeg';
            if (bytes[0] === 0x47 && bytes[1] === 0x49 && bytes[2] === 0x46) return 'image/gif';
            if (bytes[0] === 0x52 && bytes[1] === 0x49 && bytes[2] === 0x46 && bytes[3] === 0x46 &&
                bytes[8] === 0x57 && bytes[9] === 0x45 && bytes[10] === 0x42 && bytes[11] === 0x50) {
                return 'image/webp';
            }
            return '';
        }

        function prettyJson(text) {
            const trimmed = String(text || '').trim();
            if (!trimmed || (trimmed[0] !== '{' && trimmed[0] !== '[')) return '';
            try {
                return JSON.stringify(JSON.parse(trimmed), null, 2);
            } catch (error) {
                return '';
            }
        }

        function downloadCell() {
            if (!cell || !cell.save) return;
            const save = cell.save;
            const blob = new Blob([save.data], { type: save.mime });
            const url = URL.createObjectURL(blob);
            const anchor = document.createElement('a');
            anchor.href = url;
            anchor.download = save.name;
            document.body.appendChild(anchor);
            anchor.click();
            setTimeout(function () {
                URL.revokeObjectURL(url);
                anchor.remove();
            }, 800);
            toast('已保存 ' + save.name);
        }

        function bindCellViewer() {
            const dialog = byId('cellViewer');
            if (!dialog) return;
            const close = byId('cellViewerClose');
            const copy = byId('cellViewerCopy');
            const save = byId('cellViewerSave');
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
            if (save) save.addEventListener('click', downloadCell);
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
            const typeName = String(column.type || '').toUpperCase();
            const isBinary = kind === 'binary' || /BLOB|BYTEA|BINARY/.test(typeName);
            const fileName = String(column.name || 'cell').replace(/[^\w.-]+/g, '-');

            title.textContent = (column.name || '单元格') + ' · 第 ' + (info.rowIndex + 1) + ' 行';
            const parts = [];
            if (column.type) parts.push(column.type);
            else if (column.kind) parts.push(column.kind);
            if (raw == null) parts.push('NULL');
            else if (isBinary) parts.push(U.formatBytes(U.base64Bytes(raw)) + ' · Base64');
            else parts.push(U.formatNumber(String(raw).length) + ' 字符');

            U.clear(body);
            const saveBtn = byId('cellViewerSave');
            if (saveBtn) saveBtn.hidden = raw == null || !!info.noSave;
            cell.save = null;

            if (raw == null) {
                body.appendChild(U.el('div', 'cell-viewer-null', 'NULL'));
            } else if (isBinary) {
                const bytes = base64ToBytes(raw);
                const mime = imageMimeOf(bytes);
                if (mime) parts.push('图片 · ' + mime);
                body.appendChild(U.el('div', 'cell-viewer-label', 'Base64'));
                body.appendChild(U.el('pre', 'cell-viewer-pre', String(raw)));
                if (mime) {
                    const img = document.createElement('img');
                    img.className = 'cell-viewer-image';
                    img.alt = column.name || 'binary';
                    img.src = 'data:' + mime + ';base64,' + String(raw);
                    body.appendChild(img);
                }
                const hex = U.hexPreview(String(raw), 256);
                if (hex) {
                    body.appendChild(U.el('div', 'cell-viewer-label', '十六进制（前 256 字节）'));
                    body.appendChild(U.el('pre', 'cell-viewer-pre cell-viewer-hex', hex));
                }
                const ext = mime === 'image/png' ? '.png'
                    : (mime === 'image/jpeg' ? '.jpg'
                        : (mime === 'image/gif' ? '.gif'
                            : (mime === 'image/webp' ? '.webp' : '.bin')));
                cell.save = {
                    data: bytes,
                    mime: mime || 'application/octet-stream',
                    name: fileName + ext
                };
            } else {
                const text = String(raw);
                const json = prettyJson(text);
                if (json) {
                    parts.push('JSON · 已格式化');
                    body.appendChild(U.el('div', 'cell-viewer-label', 'JSON'));
                    body.appendChild(U.el('pre', 'cell-viewer-pre', json));
                    cell.save = {
                        data: json,
                        mime: 'application/json;charset=utf-8',
                        name: fileName + '.json'
                    };
                } else {
                    body.appendChild(U.el('pre', 'cell-viewer-pre', text));
                    cell.save = {
                        data: text,
                        mime: 'text/plain;charset=utf-8',
                        name: fileName + '.txt'
                    };
                }
            }

            meta.textContent = parts.join(' · ');
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
            showExplain: showExplain,
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
