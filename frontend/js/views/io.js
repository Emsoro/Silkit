// M7：导入 / 导出 / 转储 / 执行 SQL 文件。
// 一个对话框承载四种模式，入口来自结果集按钮、对象树右键和工作台工具栏。
(function () {
    'use strict';

    const U = window.SilkitUtil;

    function create(ctx) {
        const api = ctx.api;
        const state = ctx.state;
        const toast = ctx.toast;

        const dom = {};
        let task = null;
        let busy = false;
        let mode = 'export';
        let exportSource = null;
        let preview = null;
        let previewSeq = 0;

        const ENCODINGS = [
            ['utf-8', 'UTF-8'],
            ['utf-8-bom', 'UTF-8 BOM'],
            ['utf-16le', 'UTF-16 LE'],
            ['gbk', 'GBK']
        ];

        function byId(id) { return document.getElementById(id); }

        function cacheDom() {
            [
                'ioDialog', 'ioTitle', 'ioTabs', 'ioSource',
                'ioExportPane', 'ioExportFormat', 'ioExportPath', 'ioExportPick',
                'ioExportEncoding', 'ioExportDelimiter', 'ioExportDelimiterField',
                'ioExportHeader', 'ioExportHeaderField', 'ioExportNull',
                'ioImportPane', 'ioImportPath', 'ioImportPick', 'ioImportEncoding',
                'ioImportDelimiter', 'ioImportHeader', 'ioImportEmptyNull',
                'ioImportScope', 'ioImportTable', 'ioImportCreate',
                'ioImportPreviewBtn', 'ioImportSummary', 'ioImportPreview',
                'ioDumpPane', 'ioDumpPath', 'ioDumpPick', 'ioDumpScope', 'ioDumpTables',
                'ioDumpEncoding', 'ioDumpStructure', 'ioDumpData', 'ioDumpDrop',
                'ioRunPane', 'ioRunPath', 'ioRunPick', 'ioRunContinue',
                'ioProgressWrap', 'ioProgressBar', 'ioStatus', 'ioPercent',
                'ioError', 'ioFootNote', 'ioCancelBtn', 'ioCloseBtn', 'ioStartBtn',
                'ioClose'
            ].forEach(function (id) { dom[id] = byId(id); });
        }

        function fillEncodings(select) {
            if (!select) return;
            U.clear(select);
            ENCODINGS.forEach(function (item) {
                const option = U.el('option', null, item[1]);
                option.value = item[0];
                select.appendChild(option);
            });
        }

        function exportSettings() {
            return (state.settings && state.settings.export) || {};
        }

        // ------------------------------------------------------------ 打开
        function openExport(info) {
            const options = info || {};
            exportSource = { sql: (options.sql || '').trim(), ref: options.ref || null };
            setMode('export');
            setSourceLabel(options.label || (options.ref ? '表 ' + options.ref.name : '当前结果集'));
            if (dom.ioExportFormat) dom.ioExportFormat.value = options.format || 'csv';
            syncExportFields();
            if (dom.ioExportPath) {
                const hint = options.pathHint || 'export-' + timestamp();
                dom.ioExportPath.value = hint + '.' + extensionOf(dom.ioExportFormat.value);
            }
            show();
        }

        function openImport(info) {
            const options = info || {};
            setMode('import');
            setSourceLabel(options.label || '导入 CSV');
            if (dom.ioImportPath) dom.ioImportPath.value = options.path || '';
            if (dom.ioImportTable && options.ref) dom.ioImportTable.value = options.ref.name || '';
            if (dom.ioImportScope && options.ref) {
                dom.ioImportScope.value = options.ref.database && options.ref.database !== 'main'
                    ? options.ref.database : '';
            }
            if (dom.ioImportCreate) dom.ioImportCreate.checked = false;
            preview = null;
            renderPreview();
            show();
            if (options.path) refreshPreview();
        }

        function openDump(info) {
            const options = info || {};
            setMode('dump');
            setSourceLabel(options.label || 'SQL 转储');
            if (dom.ioDumpScope) dom.ioDumpScope.value = options.scope || '';
            if (dom.ioDumpTables) {
                dom.ioDumpTables.value = Array.isArray(options.tables) ? options.tables.join(', ') : '';
            }
            if (dom.ioDumpPath) dom.ioDumpPath.value = 'dump-' + timestamp() + '.sql';
            show();
        }

        function openExecuteFile() {
            setMode('run');
            setSourceLabel('执行 SQL 文件');
            if (dom.ioRunPath) dom.ioRunPath.value = '';
            if (dom.ioRunContinue) dom.ioRunContinue.checked = false;
            show();
        }

        function show() {
            if (!dom.ioDialog) return;
            setError('');
            resetProgress();
            dom.ioDialog.hidden = false;
            updateStartLabel();
        }

        function hide() {
            if (busy) {
                toast('任务进行中，请先取消或等待完成', 'error');
                return;
            }
            dom.ioDialog.hidden = true;
        }

        function setMode(next) {
            mode = next;
            if (dom.ioTabs) {
                U.qsa('[data-io-mode]', dom.ioTabs).forEach(function (button) {
                    button.classList.toggle('is-active', button.getAttribute('data-io-mode') === mode);
                });
            }
            const panes = {
                export: dom.ioExportPane,
                import: dom.ioImportPane,
                dump: dom.ioDumpPane,
                run: dom.ioRunPane
            };
            Object.keys(panes).forEach(function (key) {
                if (panes[key]) panes[key].hidden = key !== mode;
            });
            if (dom.ioSource) dom.ioSource.hidden = mode !== 'export' && mode !== 'import' && mode !== 'dump';
            syncExportFields();
            updateStartLabel();
            setError('');
            resetProgress();
        }

        function setSourceLabel(text) {
            if (dom.ioSource) dom.ioSource.textContent = text || '';
        }

        function updateStartLabel() {
            if (!dom.ioStartBtn) return;
            dom.ioStartBtn.textContent = {
                export: '开始导出',
                import: '开始导入',
                dump: '开始转储',
                run: '开始执行'
            }[mode] || '开始';
            if (dom.ioFootNote) {
                dom.ioFootNote.textContent = {
                    export: '流式写入 .part 临时文件，完成后再替换正式文件',
                    import: '批量插入并包在事务中，失败会自动回滚',
                    dump: '结构与数据写入单个 .sql 文件',
                    run: '逐条执行 SQL 语句，可随时取消'
                }[mode] || '';
            }
        }

        function syncExportFields() {
            if (!dom.ioExportFormat) return;
            const isCsv = dom.ioExportFormat.value === 'csv';
            if (dom.ioExportDelimiterField) dom.ioExportDelimiterField.hidden = !isCsv;
            if (dom.ioExportHeaderField) dom.ioExportHeaderField.hidden = !isCsv;
        }

        function extensionOf(format) {
            return {
                csv: 'csv',
                json: 'json',
                sql: 'sql',
                markdown: 'md',
                html: 'html'
            }[format] || 'csv';
        }

        function timestamp() {
            const now = new Date();
            const pad = function (value) { return String(value).padStart(2, '0'); };
            return now.getFullYear() + pad(now.getMonth() + 1) + pad(now.getDate()) + '-' +
                pad(now.getHours()) + pad(now.getMinutes()) + pad(now.getSeconds());
        }

        // ------------------------------------------------------------ 事件绑定
        function init() {
            cacheDom();
            if (!dom.ioDialog) return;

            fillEncodings(dom.ioExportEncoding);
            fillEncodings(dom.ioDumpEncoding);

            const defaults = exportSettings();
            if (dom.ioExportEncoding) dom.ioExportEncoding.value = defaults.defaultEncoding || 'utf-8';
            if (dom.ioDumpEncoding) dom.ioDumpEncoding.value = defaults.defaultEncoding || 'utf-8';
            if (dom.ioExportDelimiter) dom.ioExportDelimiter.value = defaults.csvDelimiter || ',';
            if (dom.ioExportHeader) dom.ioExportHeader.checked = defaults.includeHeader !== false;
            if (dom.ioExportNull) dom.ioExportNull.value = defaults.nullLiteral || '';

            if (dom.ioTabs) {
                dom.ioTabs.addEventListener('click', function (event) {
                    const button = event.target.closest('[data-io-mode]');
                    if (!button || busy) return;
                    setMode(button.getAttribute('data-io-mode'));
                });
            }

            if (dom.ioExportFormat) {
                dom.ioExportFormat.addEventListener('change', function () {
                    syncExportFields();
                    const path = dom.ioExportPath.value.trim();
                    if (path) {
                        const stem = path.replace(/\.[^./\\]+$/, '');
                        dom.ioExportPath.value = stem + '.' + extensionOf(dom.ioExportFormat.value);
                    }
                });
            }

            bindPath(dom.ioExportPick, dom.ioExportPath, function () {
                return { kind: extensionOf(dom.ioExportFormat.value) === 'csv' ? 'csv' : 'other', save: true,
                    defaultName: dom.ioExportPath.value ? basename(dom.ioExportPath.value) : '' };
            });
            bindPath(dom.ioImportPick, dom.ioImportPath, function () {
                return { kind: 'csv', save: false };
            }, refreshPreview);
            bindPath(dom.ioDumpPick, dom.ioDumpPath, function () {
                return { kind: 'sql', save: true, defaultName: basename(dom.ioDumpPath.value) };
            });
            bindPath(dom.ioRunPick, dom.ioRunPath, function () {
                return { kind: 'sql', save: false };
            });

            if (dom.ioImportPreviewBtn) {
                dom.ioImportPreviewBtn.addEventListener('click', function () { refreshPreview(); });
            }
            [dom.ioImportEncoding, dom.ioImportDelimiter, dom.ioImportHeader].forEach(function (input) {
                if (!input) return;
                input.addEventListener('change', function () {
                    if (dom.ioImportPath.value.trim()) refreshPreview();
                });
            });

            if (dom.ioClose) dom.ioClose.addEventListener('click', hide);
            if (dom.ioCloseBtn) dom.ioCloseBtn.addEventListener('click', hide);
            if (dom.ioDialog) {
                dom.ioDialog.addEventListener('mousedown', function (event) {
                    if (event.target === dom.ioDialog) hide();
                });
            }
            if (dom.ioStartBtn) {
                dom.ioStartBtn.addEventListener('click', function () { startTask(); });
            }
            if (dom.ioCancelBtn) {
                dom.ioCancelBtn.addEventListener('click', function () {
                    if (!task) return;
                    setStatus('正在取消…');
                    task.cancel();
                });
            }
            document.addEventListener('keydown', function (event) {
                if (event.key === 'Escape' && dom.ioDialog && !dom.ioDialog.hidden) hide();
            });
        }

        function bindPath(button, input, argsFactory, after) {
            if (!button || !input) return;
            button.addEventListener('click', function () {
                let args;
                try {
                    args = argsFactory();
                } catch (error) {
                    args = { save: true };
                }
                api.pickFile(args).then(function (result) {
                    if (result && result.path) {
                        input.value = result.path;
                        if (after) after();
                    }
                }).catch(function (error) {
                    toast('选择文件失败：' + (error.message || error), 'error');
                });
            });
        }

        function basename(path) {
            const parts = String(path || '').split(/[\\/]/);
            return parts[parts.length - 1] || '';
        }

        // ------------------------------------------------------------ 导入预览
        async function refreshPreview() {
            const path = dom.ioImportPath.value.trim();
            if (!path) {
                preview = null;
                renderPreview();
                return;
            }
            const seq = ++previewSeq;
            setPreviewSummary('正在读取预览…');
            setError('');
            try {
                const result = await api.importPreview({
                    path: path,
                    encoding: dom.ioImportEncoding.value,
                    delimiter: dom.ioImportDelimiter.value,
                    header: dom.ioImportHeader.checked
                });
                if (seq !== previewSeq) return;
                preview = result || null;
                if (preview) {
                    if (!dom.ioImportTable.value.trim()) {
                        dom.ioImportTable.value = guessTableName(path);
                    }
                    setPreviewSummary('编码 ' + preview.encoding + ' · 分隔符 "' + preview.delimiter +
                        '" · 共 ' + U.formatNumber(preview.total) + ' 行数据 · ' +
                        U.formatBytes(preview.bytes));
                }
                renderPreview();
            } catch (error) {
                if (seq !== previewSeq) return;
                preview = null;
                renderPreview();
                setPreviewSummary('预览失败');
                setError('预览失败：' + (error.message || error));
            }
        }

        function guessTableName(path) {
            let name = basename(path).replace(/\.[^.]+$/, '');
            name = name.replace(/[^0-9A-Za-z_\u4e00-\u9fa5]+/g, '_').replace(/^_+|_+$/g, '');
            return name || 'imported';
        }

        function setPreviewSummary(text) {
            if (dom.ioImportSummary) dom.ioImportSummary.textContent = text;
        }

        function renderPreview() {
            const host = dom.ioImportPreview;
            if (!host) return;
            U.clear(host);
            if (!preview || !Array.isArray(preview.rows) || !preview.columns.length) {
                host.appendChild(U.el('p', 'muted', '选择 CSV 文件后自动预览前 20 行'));
                return;
            }
            const scroll = U.el('div', 'io-preview-scroll');
            const table = U.el('table', 'io-preview-table');

            const thead = U.el('thead');
            const headRow = U.el('tr');
            preview.columns.forEach(function (column, index) {
                const th = U.el('th');
                const input = U.el('input', 'input mono io-map-input');
                input.value = column.name || '';
                input.setAttribute('data-column', String(index));
                input.title = '目标列名，留空则跳过';
                th.appendChild(input);
                headRow.appendChild(th);
            });
            thead.appendChild(headRow);
            table.appendChild(thead);

            const tbody = U.el('tbody');
            preview.rows.slice(0, 8).forEach(function (row) {
                const tr = U.el('tr');
                preview.columns.forEach(function (column, index) {
                    const value = row[index] == null ? '' : String(row[index]);
                    tr.appendChild(U.el('td', null, value.length > 120 ? value.slice(0, 120) + '…' : value));
                });
                tbody.appendChild(tr);
            });
            table.appendChild(tbody);
            scroll.appendChild(table);
            host.appendChild(scroll);
            host.appendChild(U.el('p', 'io-hint', '首行输入框为目标列名，留空表示跳过该列；预览只展示前 8 行。'));
        }

        // ------------------------------------------------------------ 任务
        function startTask() {
            if (busy) return;
            if (!state.activeSession()) {
                setError('请先连接一个数据库');
                return;
            }
            if (mode === 'export') startExport();
            else if (mode === 'import') startImport();
            else if (mode === 'dump') startDump();
            else startRun();
        }

        function startExport() {
            const path = dom.ioExportPath.value.trim();
            if (!path) {
                setError('请选择导出文件路径');
                return;
            }
            const session = state.activeSession();
            const args = {
                sessionId: session.id,
                path: path,
                format: dom.ioExportFormat.value,
                encoding: dom.ioExportEncoding.value,
                includeHeader: dom.ioExportHeader.checked,
                delimiter: dom.ioExportDelimiter.value,
                nullLiteral: dom.ioExportNull.value
            };
            if (exportSource && exportSource.sql) {
                args.sql = exportSource.sql;
            } else if (exportSource && exportSource.ref) {
                args.ref = exportSource.ref;
            } else {
                setError('没有可导出的 SQL 或表');
                return;
            }

            runTask(api.exportResult(args, {
                onProgress: function (data) {
                    setStatus('已写入 ' + U.formatNumber(data.rows || 0) + ' 行 · ' +
                        U.formatBytes(data.bytes || 0));
                    setPercent(-1);
                }
            }), function (result) {
                setStatus('导出完成：' + U.formatNumber(result.rows || 0) + ' 行 · ' +
                    U.formatBytes(result.bytes || 0));
                toast('已导出 ' + U.formatNumber(result.rows || 0) + ' 行到 ' +
                    basename(result.path || path));
            }, function (error) {
                return '导出失败：' + error.message;
            });
        }

        function startImport() {
            const path = dom.ioImportPath.value.trim();
            if (!path) {
                setError('请选择 CSV 文件');
                return;
            }
            if (!preview) {
                setError('请先预览 CSV，确认列映射');
                return;
            }
            const table = dom.ioImportTable.value.trim();
            if (!table) {
                setError('请填写目标表名');
                return;
            }
            const mappingInputs = U.qsa('.io-map-input', dom.ioImportPreview);
            const columns = preview.columns.map(function (column, index) {
                const input = mappingInputs[index];
                const name = input ? input.value.trim() : column.name;
                return { index: column.index, name: name, type: '' };
            }).filter(function (column) { return !!column.name; });
            if (!columns.length) {
                setError('请至少映射一个目标列');
                return;
            }

            const session = state.activeSession();
            const args = {
                sessionId: session.id,
                path: path,
                encoding: dom.ioImportEncoding.value,
                delimiter: dom.ioImportDelimiter.value,
                header: dom.ioImportHeader.checked,
                emptyAsNull: dom.ioImportEmptyNull.checked,
                scope: dom.ioImportScope.value.trim(),
                table: table,
                createTable: dom.ioImportCreate.checked,
                columns: columns,
                batchSize: 500
            };

            runTask(api.importCsv(args, {
                onProgress: function (data) {
                    const total = Number(data.total) || 0;
                    const rows = Number(data.rows) || 0;
                    setStatus('已导入 ' + U.formatNumber(rows) + ' / ' + U.formatNumber(total) + ' 行');
                    setPercent(total > 0 ? Math.round(rows * 100 / total) : -1);
                }
            }), function (result) {
                setStatus('导入完成：' + U.formatNumber(result.rows || 0) + ' 行');
                toast('已导入 ' + U.formatNumber(result.rows || 0) + ' 行到 ' + result.table);
                refreshSession(session);
            }, function (error) {
                const details = error.details || {};
                if (details.rolledBack) {
                    return '导入失败，已回滚' + (details.record ? '（第 ' + details.record + ' 条数据，文件第 ' +
                        details.line + ' 行）' : '') + '：' + error.message;
                }
                return '导入失败：' + error.message;
            });
        }

        function startDump() {
            const path = dom.ioDumpPath.value.trim();
            if (!path) {
                setError('请选择转储文件路径');
                return;
            }
            const session = state.activeSession();
            const tables = dom.ioDumpTables.value.split(',')
                .map(function (item) { return item.trim(); })
                .filter(function (item) { return !!item; });
            const args = {
                sessionId: session.id,
                path: path,
                scope: dom.ioDumpScope.value.trim(),
                tables: tables,
                includeStructure: dom.ioDumpStructure.checked,
                includeData: dom.ioDumpData.checked,
                dropIfExists: dom.ioDumpDrop.checked,
                encoding: dom.ioDumpEncoding.value
            };

            runTask(api.dumpSql(args, {
                onProgress: function (data) {
                    const total = Number(data.total) || 0;
                    const done = Number(data.tableIndex) || 0;
                    setStatus('正在转储 ' + (data.table || '') + '（' + done + '/' + total + '）');
                    setPercent(total > 0 ? Math.round(done * 100 / total) : -1);
                }
            }), function (result) {
                setStatus('转储完成：' + U.formatNumber(result.tables || 0) + ' 表 / ' +
                    U.formatNumber(result.rows || 0) + ' 行');
                toast('已转储 ' + U.formatNumber(result.tables || 0) + ' 张表到 ' +
                    basename(result.path || path));
            }, function (error) {
                return '转储失败：' + error.message;
            });
        }

        function startRun() {
            const path = dom.ioRunPath.value.trim();
            if (!path) {
                setError('请选择 SQL 文件');
                return;
            }
            const session = state.activeSession();
            const args = {
                sessionId: session.id,
                path: path,
                continueOnError: dom.ioRunContinue.checked
            };

            runTask(api.queryExecuteFile(args, {
                onProgress: function (data) {
                    const total = Number(data.total) || 0;
                    const index = Number(data.index) || 0;
                    setStatus('已执行 ' + index + ' / ' + total + ' 条语句');
                    setPercent(total > 0 ? Math.round(index * 100 / total) : -1);
                },
                onEvent: function (type, data) {
                    if (type !== 'statement') return;
                    const previewText = String(data.sql || '').replace(/\s+/g, ' ').slice(0, 72);
                    setStatus('执行第 ' + (Number(data.index) + 1) + ' / ' + data.total + ' 条：' + previewText);
                }
            }), function (result) {
                const count = result.statements ? result.statements.length : 0;
                setStatus('执行完成：' + count + ' 条语句');
                toast('SQL 文件执行完成（' + count + ' 条语句）');
                refreshSession(session);
            }, function (error) {
                const details = error.details || {};
                if (typeof details.statementIndex === 'number') {
                    return '第 ' + (details.statementIndex + 1) + ' 条语句失败' +
                        (typeof details.offset === 'number' ? '（偏移 ' + details.offset + '）' : '') +
                        '：' + error.message;
                }
                return '执行失败：' + error.message;
            });
        }

        function runTask(current, onSuccess, onErrorText) {
            busy = true;
            task = current;
            dom.ioStartBtn.disabled = true;
            dom.ioCancelBtn.hidden = false;
            setError('');
            resetProgress();
            setStatus('准备中…');

            current.promise.then(function (result) {
                if (result && result.cancelled) {
                    setStatus('已取消，临时文件已清理');
                    toast('任务已取消');
                    return;
                }
                onSuccess(result || {});
            }).catch(function (error) {
                if (error && error.code === 'cancelled') {
                    setStatus('已取消，临时文件已清理');
                    toast('任务已取消');
                    return;
                }
                const text = onErrorText(error || {});
                setStatus('失败');
                setError(text);
            }).then(function () {
                busy = false;
                task = null;
                dom.ioStartBtn.disabled = false;
                dom.ioCancelBtn.hidden = true;
            });
        }

        function refreshSession(session) {
            if (!session) return;
            if (ctx.tree) ctx.tree.refreshSession(session.id);
            if (ctx.hint) ctx.hint.invalidate(session.id);
        }

        // ------------------------------------------------------------ 进度 / 错误
        function resetProgress() {
            if (dom.ioProgressWrap) dom.ioProgressWrap.hidden = true;
            if (dom.ioProgressBar) dom.ioProgressBar.style.width = '0%';
            if (dom.ioPercent) dom.ioPercent.textContent = '';
            if (dom.ioStatus) dom.ioStatus.textContent = '';
        }

        function setStatus(text, percent) {
            if (dom.ioProgressWrap) dom.ioProgressWrap.hidden = false;
            if (dom.ioStatus) dom.ioStatus.textContent = text || '';
            if (typeof percent === 'number') setPercent(percent);
        }

        function setPercent(percent) {
            if (!dom.ioProgressBar) return;
            if (percent < 0) {
                dom.ioProgressWrap.classList.add('is-indeterminate');
                dom.ioProgressBar.style.width = '100%';
                if (dom.ioPercent) dom.ioPercent.textContent = '';
                return;
            }
            dom.ioProgressWrap.classList.remove('is-indeterminate');
            dom.ioProgressBar.style.width = Math.max(0, Math.min(100, percent)) + '%';
            if (dom.ioPercent) dom.ioPercent.textContent = percent + '%';
        }

        function setError(text) {
            if (!dom.ioError) return;
            dom.ioError.hidden = !text;
            dom.ioError.textContent = text || '';
        }

        return {
            init: init,
            openExport: openExport,
            openImport: openImport,
            openDump: openDump,
            openExecuteFile: openExecuteFile
        };
    }

    window.SilkitIo = { create: create };
})();
