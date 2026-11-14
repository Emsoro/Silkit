// 表数据视图（M5）：分页/排序/筛选/搜索、网格编辑状态机、SQL 预览与事务保存。
//
//   const table = SilkitTable.create(ctx);   // ctx: api/state/toast/results/tabs
//   table.init();
//   table.show(tab);      // 激活 table 标签时调用
//   table.save(tab);      // Ctrl+S
//   table.hasChanges(tab);
(function () {
    'use strict';

    const U = window.SilkitUtil;

    const PAGE_SIZES = [50, 100, 200, 500, 1000];

    const OPS = [
        { value: '=', label: '=' },
        { value: '<>', label: '≠' },
        { value: '>', label: '>' },
        { value: '>=', label: '≥' },
        { value: '<', label: '<' },
        { value: '<=', label: '≤' },
        { value: 'LIKE', label: '包含' },
        { value: 'IN', label: 'IN' },
        { value: 'IS NULL', label: '为 NULL' },
        { value: 'IS NOT NULL', label: '非 NULL' }
    ];

    function create(ctx) {
        const api = ctx.api;
        const state = ctx.state;
        const toast = ctx.toast;

        const dom = {};
        let grid = null;
        let newSeq = 0;

        function byId(id) { return document.getElementById(id); }

        function init() {
            const ids = ['tablePane', 'tableName', 'tableBadge', 'tableInsertBtn', 'tableDeleteBtn',
                'tableRevertBtn', 'tableSaveBtn', 'tableReloadBtn', 'tableCountBtn', 'tableTruncateBtn',
                'tableSearch', 'tableFilterColumn', 'tableFilterOp', 'tableFilterValue',
                'tableFilterAdd', 'tableFilterClear', 'tableFilterChips', 'tableGrid', 'tableStatus',
                'tablePrev', 'tableNext', 'tablePage', 'tablePageSize', 'tableTotal', 'tableError',
                'tableViewCellBtn', 'tablePreview', 'tablePreviewList', 'tablePreviewWarn',
                'tablePreviewCancel', 'tablePreviewRun', 'tablePreviewMeta', 'tablePreviewClose'];
            ids.forEach(function (id) { dom[id] = byId(id); });
            if (!dom.tableGrid) return;

            grid = window.SilkitGrid.create(dom.tableGrid, {
                emptyMessage: '没有数据',
                emptyHint: '调整筛选条件，或点击「新增行」开始编辑',
                canEdit: function (row, col) {
                    const tab = state.activeTab();
                    return !!(tab && tab.kind === 'table' && canEditCell(tab.table, row, col));
                },
                rowClass: function (row) {
                    const tab = state.activeTab();
                    return tab && tab.table ? rowClass(tab.table, row) : '';
                },
                cellClass: function (row, col) {
                    const tab = state.activeTab();
                    return tab && tab.table ? cellClass(tab.table, row, col) : '';
                },
                sortState: function () {
                    const tab = state.activeTab();
                    return tab && tab.table ? tab.table.order : null;
                },
                onSortColumn: function (index) {
                    const tab = state.activeTab();
                    if (tab) toggleSort(tab, index);
                },
                onEditCell: function (row, col, text, isNull) {
                    const tab = state.activeTab();
                    if (tab) applyCellEdit(tab, row, col, text, isNull);
                },
                onOpenCell: function (info) {
                    const tab = state.activeTab();
                    if (tab) viewCell(tab, info.rowIndex, info.colIndex);
                },
                onToast: function (message, variant) { toast(message, variant); }
            });

            bindToolbar();
            bindFilterBar();
            bindPreview();

            dom.tableGrid.addEventListener('contextmenu', function (event) {
                const cell = event.target.closest('.vgrid-cell[data-col]');
                if (!cell) return;
                const col = Number(cell.getAttribute('data-col'));
                const row = Number(cell.getAttribute('data-row'));
                if (!Number.isFinite(row) || !Number.isFinite(col) || col < 0) return;
                event.preventDefault();
                const tab = state.activeTab();
                if (tab) viewCell(tab, row, col);
            });
        }

        // ------------------------------------------------------------ 状态
        function ensure(tab) {
            if (!tab.table) {
                const configured = state.settings && state.settings.data &&
                    Number(state.settings.data.pageSize);
                tab.table = {
                    ref: tab.ref || {},
                    page: 0,
                    pageSize: PAGE_SIZES.indexOf(configured) >= 0 ? configured : 500,
                    order: null,
                    filters: [],
                    search: '',
                    columns: [],
                    identity: { type: 'none', columns: [] },
                    editable: false,
                    readOnlyReason: '',
                    serverRows: [],
                    rowKeys: [],
                    rows: [],
                    newRows: [],
                    display: [],
                    gridRows: [],
                    gridSet: null,
                    hasMore: false,
                    totalEstimate: null,
                    elapsedMs: 0,
                    warnings: [],
                    loading: false,
                    loaded: false,
                    loadToken: 0,
                    error: null,
                    errorRow: null,
                    preview: null
                };
            }
            return tab.table;
        }

        function sessionFor(tab) {
            return tab && tab.sessionId ? state.session(tab.sessionId) : null;
        }

        function canEditCell(t, rowIndex, colIndex) {
            if (!t || !t.editable || t.loading) return false;
            const row = t.display[rowIndex];
            const column = t.columns[colIndex];
            if (!row || !column || row.deleted) return false;
            if (column.generated) return false;
            return true;
        }

        function dirtyCount(t) {
            let count = t.newRows.length;
            t.rows.forEach(function (row) {
                if (row.deleted) count += 1;
                else count += Object.keys(row.dirty).length;
            });
            return count;
        }

        function hasChanges(tab) {
            const t = tab && tab.table;
            if (!t) return false;
            return dirtyCount(t) > 0;
        }

        function rowClass(t, rowIndex) {
            const row = t.display[rowIndex];
            if (!row) return '';
            if (row.kind === 'new') return 'is-new';
            if (t.errorRow === rowIndex) return 'is-error-row';
            if (row.deleted) return 'is-deleted';
            if (Object.keys(row.dirty).length) return 'is-edited';
            return '';
        }

        function cellClass(t, rowIndex, colIndex) {
            const row = t.display[rowIndex];
            const column = t.columns[colIndex];
            if (!row || !column) return '';
            if (row.deleted) return 'is-deleted-cell';
            return Object.prototype.hasOwnProperty.call(row.dirty, column.name) ? 'is-dirty-cell' : '';
        }

        function updateDirty(tab) {
            const pending = hasChanges(tab);
            tab.dirty = pending;
            if (ctx.tabs && ctx.tabs.render) ctx.tabs.render();
            updateStatus(tab);
        }

        // ------------------------------------------------------------ 加载
        async function load(tab, options) {
            const opts = options || {};
            const t = ensure(tab);
            const session = sessionFor(tab);
            if (!session) {
                t.columns = [];
                renderEmpty('未连接', '重新打开连接后即可浏览表数据');
                dom.tableBadge.textContent = '未连接';
                dom.tableName.textContent = tab.title || '表';
                return;
            }
            if (!opts.keepEdits) resetEdits(t);
            const token = ++t.loadToken;
            t.loading = true;
            t.error = null;
            t.errorRow = null;
            setError('');
            updateStatus(tab);
            try {
                const data = await api.tableRows(session.id, t.ref, {
                    page: t.page,
                    pageSize: t.pageSize,
                    order: t.order || undefined,
                    filters: t.filters,
                    search: t.search
                });
                if (token !== t.loadToken) return;
                t.columns = Array.isArray(data.columns) ? data.columns : [];
                t.identity = data.identity || { type: 'none', columns: [] };
                t.editable = !!data.editable;
                t.readOnlyReason = data.readOnlyReason || '';
                t.serverRows = Array.isArray(data.rows) ? data.rows : [];
                t.rowKeys = Array.isArray(data.rowKeys) ? data.rowKeys : [];
                t.page = Number(data.page) || 0;
                t.pageSize = Number(data.pageSize) || t.pageSize;
                t.hasMore = !!data.hasMore;
                t.totalEstimate = data.totalEstimate == null ? null : Number(data.totalEstimate);
                t.elapsedMs = Number(data.elapsedMs) || 0;
                t.warnings = Array.isArray(data.warnings) ? data.warnings : [];
                t.loading = false;
                t.loaded = true;
                if (t.editable && !t.columns.length) t.editable = false;
                rebuild(tab, true);
                renderFilterColumns(tab);
                renderFilters(tab);
                if (t.warnings.length) toast(t.warnings[0], 'warn');
            } catch (error) {
                if (token !== t.loadToken) return;
                t.loading = false;
                t.loaded = true;
                t.serverRows = [];
                t.rowKeys = [];
                rebuild(tab, true);
                setError('加载失败：' + ((error && error.message) || error));
                toast('加载表数据失败：' + ((error && error.message) || error), 'error');
                updateStatus(tab);
            }
        }

        function renderEmpty(message, hint) {
            if (!grid) return;
            grid.setEmpty(message, hint);
        }

        function resetEdits(t) {
            t.newRows = [];
            t.rows.forEach(function (row) {
                row.dirty = {};
                row.deleted = false;
            });
            t.error = null;
            t.errorRow = null;
        }

        function rebuild(tab, resetScroll) {
            const t = ensure(tab);
            const columns = t.columns;
            const previous = t.rows || [];
            t.rows = t.serverRows.map(function (values, index) {
                const old = previous[index];
                return {
                    kind: 'server',
                    serverIndex: index,
                    values: values.slice(),
                    original: values.slice(),
                    keys: t.rowKeys[index] || {},
                    dirty: old ? old.dirty : {},
                    deleted: old ? old.deleted : false
                };
            });
            t.newRows.forEach(function (row) {
                row.values = columns.map(function (_, index) {
                    return Object.prototype.hasOwnProperty.call(row.dirty, columns[index].name)
                        ? row.dirty[columns[index].name]
                        : undefined;
                });
            });
            t.display = t.rows.concat(t.newRows);
            t.gridRows = t.display.map(function (row) { return row.values; });

            if (!t.gridSet) {
                t.gridSet = { columns: columns, rows: t.gridRows, widths: null };
            } else {
                const widths = t.gridSet.widths;
                t.gridSet.columns = columns;
                t.gridSet.rows = t.gridRows;
                t.gridSet.widths = widths && widths.length === columns.length ? widths : null;
            }

            if (!columns.length) {
                renderEmpty(t.editable === false && t.readOnlyReason ? '只读' : '没有数据',
                    t.readOnlyReason || '调整筛选或分页条件');
                renderBadge(t);
                updateStatus(tab);
                return;
            }
            grid.setSet(t.gridSet);
            if (resetScroll && dom.tableGrid) dom.tableGrid.scrollTop = 0;
            renderBadge(t);
            updateStatus(tab);
        }

        function refreshGrid(tab) {
            const t = ensure(tab);
            t.display = t.rows.concat(t.newRows);
            t.display.forEach(function (row) {
                row.values = row.values || [];
            });
            t.gridRows = t.display.map(function (row) { return row.values; });
            if (t.gridSet) t.gridSet.rows = t.gridRows;
            grid.refresh();
            updateStatus(tab);
        }

        function renderBadge(t) {
            if (!dom.tableBadge) return;
            if (t.editable) {
                const identity = t.identity && t.identity.type;
                dom.tableBadge.textContent = identity === 'pk' ? '可编辑 · 主键'
                    : (identity === 'rowid' ? '可编辑 · rowid' : '可编辑 · 唯一索引');
                dom.tableBadge.className = 'table-badge is-editable';
                dom.tableBadge.title = '行标识：' + (t.identity.columns || []).join(', ');
            } else {
                dom.tableBadge.textContent = '只读';
                dom.tableBadge.className = 'table-badge is-readonly';
                dom.tableBadge.title = t.readOnlyReason || '当前表不可编辑';
            }
            if (dom.tableName) dom.tableName.textContent = (t.ref && t.ref.name) || '表';
        }

        function updateStatus(tab) {
            if (!dom.tableStatus) return;
            const t = ensure(tab);
            const parts = [];
            if (t.loading) {
                parts.push('加载中…');
            } else {
                parts.push('第 ' + (t.page + 1) + ' 页');
                parts.push('本页 ' + t.serverRows.length + ' 行');
                if (t.totalEstimate != null) {
                    parts.push((t.totalEstimate < 0 ? '≈ ' : '') + U.formatNumber(t.totalEstimate) + ' 行');
                }
                if (t.elapsedMs) parts.push(U.formatDuration(t.elapsedMs));
            }
            const pending = dirtyCount(t);
            if (pending) parts.push('未保存 ' + pending + ' 处');
            if (t.error) parts.push('错误：' + t.error);
            dom.tableStatus.textContent = parts.join(' · ');
            dom.tableStatus.classList.toggle('is-error', !!t.error);
            dom.tableStatus.classList.toggle('is-dirty', pending > 0);

            if (dom.tablePage) dom.tablePage.textContent = '第 ' + (t.page + 1) + ' 页';
            if (dom.tableTotal) {
                dom.tableTotal.textContent = t.totalEstimate == null
                    ? ''
                    : (t.totalEstimate < 0 ? '≈ ' : '') + U.formatNumber(t.totalEstimate) + ' 行';
            }
            if (dom.tablePrev) dom.tablePrev.disabled = t.page <= 0 || t.loading;
            if (dom.tableNext) dom.tableNext.disabled = !t.hasMore || t.loading;
            if (dom.tableSaveBtn) {
                dom.tableSaveBtn.disabled = !pending || t.loading;
                dom.tableSaveBtn.classList.toggle('is-accent', pending > 0);
            }
            if (dom.tableRevertBtn) dom.tableRevertBtn.disabled = !pending || t.loading;
            if (dom.tableInsertBtn) dom.tableInsertBtn.disabled = !t.editable || t.loading;
            if (dom.tableDeleteBtn) dom.tableDeleteBtn.disabled = !t.editable || t.loading;
            if (dom.tableTruncateBtn) dom.tableTruncateBtn.disabled = !t.editable || t.loading;
            if (dom.tablePageSize && String(t.pageSize) !== dom.tablePageSize.value) {
                dom.tablePageSize.value = String(t.pageSize);
            }
        }

        function setError(message) {
            if (!dom.tableError) return;
            dom.tableError.textContent = message || '';
            dom.tableError.hidden = !message;
        }

        // ------------------------------------------------------------ 编辑
        function applyCellEdit(tab, rowIndex, colIndex, text, isNull) {
            const t = ensure(tab);
            const row = t.display[rowIndex];
            const column = t.columns[colIndex];
            if (!row || !column) return;
            if (row.kind === 'server' && row.deleted) return;
            const value = isNull ? null : text;
            row.values[colIndex] = value;

            if (row.kind === 'new') {
                row.dirty[column.name] = value;
            } else {
                const original = row.original[colIndex];
                const originalValue = original == null ? null : String(original);
                const nextValue = value == null ? null : String(value);
                if (originalValue === nextValue) delete row.dirty[column.name];
                else row.dirty[column.name] = value;
            }
            t.error = null;
            t.errorRow = null;
            grid.refresh();
            updateDirty(tab);
        }

        function addRow(tab) {
            const t = ensure(tab);
            if (!t.editable) {
                toast(t.readOnlyReason || '该表只读', 'warn');
                return;
            }
            const row = {
                kind: 'new',
                clientId: 'n' + (++newSeq),
                values: t.columns.map(function () { return undefined; }),
                dirty: {},
                deleted: false
            };
            t.newRows.push(row);
            rebuild(tab, false);
            updateDirty(tab);
            const displayIndex = t.rows.length + t.newRows.length - 1;
            grid.setFocusCell(displayIndex, 0);
            grid.focus();
            const first = t.columns.findIndex(function (column) {
                return !column.generated && !(column.primaryKey && column.autoIncrement);
            });
            if (first >= 0) grid.beginEdit(displayIndex, first);
        }

        function deleteSelected(tab) {
            const t = ensure(tab);
            if (!t.editable) {
                toast(t.readOnlyReason || '该表只读', 'warn');
                return;
            }
            const selected = grid.selectedRows();
            if (!selected.length) {
                toast('请先在网格中选择要删除的行', 'warn');
                return;
            }
            let removedNew = 0;
            let marked = 0;
            selected.forEach(function (index) {
                const row = t.display[index];
                if (!row) return;
                if (row.kind === 'new') {
                    const at = t.newRows.indexOf(row);
                    if (at >= 0) { t.newRows.splice(at, 1); removedNew += 1; }
                } else if (!row.deleted) {
                    row.deleted = true;
                    marked += 1;
                }
            });
            if (!removedNew && !marked) return;
            t.error = null;
            t.errorRow = null;
            if (removedNew) rebuild(tab, false);
            else refreshGrid(tab);
            updateDirty(tab);
            toast('已标记 ' + (removedNew + marked) + ' 行待删除（保存后生效）');
        }

        function revert(tab) {
            const t = ensure(tab);
            if (!dirtyCount(t)) return;
            if (!window.confirm('撤销当前页所有未保存的修改？')) return;
            resetEdits(t);
            rebuild(tab, false);
            updateDirty(tab);
            toast('已撤销未保存的修改');
        }

        function viewCell(tab, rowIndex, colIndex) {
            const t = ensure(tab);
            const row = t.display[rowIndex];
            const column = t.columns[colIndex];
            if (!row || !column) return;
            const session = sessionFor(tab);
            const fallback = {
                column: column,
                value: row.values[colIndex],
                rowIndex: rowIndex,
                colIndex: colIndex,
                row: row.values
            };
            if (!session || row.kind === 'new' || !row.keys || !Object.keys(row.keys).length) {
                if (ctx.results) ctx.results.openCell(fallback);
                return;
            }
            api.tableCell(session.id, t.ref, column.name, row.keys).then(function (data) {
                if (ctx.results) {
                    ctx.results.openCell({
                        column: Object.assign({}, column, {
                            type: data.columnType || column.type,
                            kind: data.kind || column.kind
                        }),
                        value: data.isNull ? null : data.text,
                        rowIndex: rowIndex,
                        colIndex: colIndex,
                        row: row.values
                    });
                }
            }).catch(function (error) {
                if (error && error.code === 'not_found') {
                    toast('该行已不存在或已被其他会话修改', 'warn');
                    load(tab);
                    return;
                }
                toast('读取单元格失败：' + ((error && error.message) || error), 'error');
                if (ctx.results) ctx.results.openCell(fallback);
            });
        }

        // ------------------------------------------------------------ 排序/分页/筛选
        function toggleSort(tab, index) {
            const t = ensure(tab);
            const column = t.columns[index];
            if (!column) return;
            if (hasChanges(tab) &&
                !window.confirm('切换排序会丢弃当前页未保存的修改，继续？')) return;
            if (!t.order || t.order.column !== column.name) {
                t.order = { column: column.name, desc: false };
            } else if (!t.order.desc) {
                t.order.desc = true;
            } else {
                t.order = null;
            }
            if (grid) grid.refresh();
            load(tab);
        }

        function pageBy(tab, delta) {
            const t = ensure(tab);
            const next = t.page + delta;
            if (next < 0 || (delta > 0 && !t.hasMore)) return;
            if (hasChanges(tab) &&
                !window.confirm('切换分页会丢弃当前页未保存的修改，继续？')) return;
            t.page = next;
            load(tab);
        }

        function reload(tab) {
            const t = ensure(tab);
            t.error = null;
            t.errorRow = null;
            load(tab);
        }

        async function count(tab) {
            const t = ensure(tab);
            const session = sessionFor(tab);
            if (!session) return;
            try {
                const data = await api.tableCount(session.id, t.ref, {
                    page: t.page,
                    pageSize: t.pageSize,
                    filters: t.filters,
                    search: t.search
                });
                t.totalEstimate = Number(data.count) || 0;
                updateStatus(tab);
                toast('精确计数：' + U.formatNumber(t.totalEstimate) + ' 行');
            } catch (error) {
                toast('计数失败：' + ((error && error.message) || error), 'error');
            }
        }

        function applyFilters(tab) {
            const t = ensure(tab);
            if (hasChanges(tab) &&
                !window.confirm('更改搜索/筛选会丢弃当前页未保存的修改，继续？')) {
                return false;
            }
            t.page = 0;
            load(tab);
            return true;
        }

        function bindFilterBar() {
            let timer = 0;
            if (dom.tableSearch) {
                dom.tableSearch.addEventListener('input', function () {
                    clearTimeout(timer);
                    timer = setTimeout(function () {
                        const tab = state.activeTab();
                        if (!tab || tab.kind !== 'table') return;
                        const t = ensure(tab);
                        const previous = t.search;
                        const next = dom.tableSearch.value.trim();
                        if (next === previous) return;
                        t.search = next;
                        if (!applyFilters(tab)) {
                            t.search = previous;
                            dom.tableSearch.value = previous;
                        }
                    }, 320);
                });
            }
            if (dom.tableFilterOp) {
                OPS.forEach(function (op) {
                    const option = document.createElement('option');
                    option.value = op.value;
                    option.textContent = op.label;
                    dom.tableFilterOp.appendChild(option);
                });
                dom.tableFilterOp.addEventListener('change', syncFilterValueState);
            }
            if (dom.tableFilterAdd) {
                dom.tableFilterAdd.addEventListener('click', function () {
                    const tab = state.activeTab();
                    if (tab) addFilter(tab);
                });
            }
            if (dom.tableFilterValue) {
                dom.tableFilterValue.addEventListener('keydown', function (event) {
                    if (event.key === 'Enter') {
                        event.preventDefault();
                        const tab = state.activeTab();
                        if (tab) addFilter(tab);
                    }
                });
            }
            if (dom.tableFilterClear) {
                dom.tableFilterClear.addEventListener('click', function () {
                    const tab = state.activeTab();
                    if (!tab) return;
                    const t = ensure(tab);
                    const previousFilters = t.filters;
                    const previousSearch = t.search;
                    t.filters = [];
                    t.search = '';
                    if (dom.tableSearch) dom.tableSearch.value = '';
                    renderFilters(tab);
                    if (!applyFilters(tab)) {
                        t.filters = previousFilters;
                        t.search = previousSearch;
                        if (dom.tableSearch) dom.tableSearch.value = previousSearch;
                        renderFilters(tab);
                    }
                });
            }
            if (dom.tableFilterChips) {
                dom.tableFilterChips.addEventListener('click', function (event) {
                    const button = event.target.closest('[data-filter-index]');
                    if (!button) return;
                    const tab = state.activeTab();
                    if (!tab) return;
                    const t = ensure(tab);
                    const index = Number(button.getAttribute('data-filter-index'));
                    const removed = t.filters.splice(index, 1)[0];
                    renderFilters(tab);
                    if (!applyFilters(tab)) {
                        t.filters.splice(index, 0, removed);
                        renderFilters(tab);
                    }
                });
            }
            syncFilterValueState();
        }

        function syncFilterValueState() {
            if (!dom.tableFilterValue || !dom.tableFilterOp) return;
            const op = dom.tableFilterOp.value;
            const needsValue = op !== 'IS NULL' && op !== 'IS NOT NULL';
            dom.tableFilterValue.disabled = !needsValue;
            dom.tableFilterValue.placeholder = op === 'IN' ? '值1, 值2, …' : '值';
        }

        function addFilter(tab) {
            const t = ensure(tab);
            const column = dom.tableFilterColumn && dom.tableFilterColumn.value;
            if (!column) {
                toast('请选择筛选列', 'warn');
                return;
            }
            const op = (dom.tableFilterOp && dom.tableFilterOp.value) || '=';
            const needsValue = op !== 'IS NULL' && op !== 'IS NOT NULL';
            const value = dom.tableFilterValue ? dom.tableFilterValue.value : '';
            if (needsValue && !value.trim()) {
                toast('请输入筛选值', 'warn');
                return;
            }
            const filter = { column: column, op: op };
            if (needsValue) {
                if (op === 'IN') {
                    filter.value = value.split(',').map(function (item) { return item.trim(); })
                        .filter(function (item) { return item.length > 0; });
                } else {
                    filter.value = value;
                }
            }
            t.filters.push(filter);
            if (dom.tableFilterValue) dom.tableFilterValue.value = '';
            renderFilters(tab);
            if (!applyFilters(tab)) {
                t.filters.pop();
                renderFilters(tab);
            }
        }

        function renderFilters(tab) {
            const t = ensure(tab);
            if (!dom.tableFilterChips) return;
            U.clear(dom.tableFilterChips);
            t.filters.forEach(function (filter, index) {
                const chip = U.el('span', 'filter-chip');
                const label = filter.column + ' ' + filter.op +
                    (filter.value === undefined ? '' :
                        ' ' + (Array.isArray(filter.value) ? filter.value.join(',') : filter.value));
                chip.appendChild(U.el('span', 'filter-chip-text', label));
                const remove = U.el('button', 'filter-chip-close', '×');
                remove.type = 'button';
                remove.title = '移除筛选';
                remove.setAttribute('data-filter-index', String(index));
                chip.appendChild(remove);
                dom.tableFilterChips.appendChild(chip);
            });
            if (!t.filters.length) {
                dom.tableFilterChips.appendChild(U.el('span', 'filter-empty', '没有筛选条件'));
            }
        }

        function renderFilterColumns(tab) {
            if (!dom.tableFilterColumn) return;
            const t = ensure(tab);
            const current = dom.tableFilterColumn.value;
            U.clear(dom.tableFilterColumn);
            t.columns.forEach(function (column) {
                const option = document.createElement('option');
                option.value = column.name;
                option.textContent = column.name;
                dom.tableFilterColumn.appendChild(option);
            });
            if (current && t.columns.some(function (column) { return column.name === current; })) {
                dom.tableFilterColumn.value = current;
            }
        }

        // ------------------------------------------------------------ 保存
        function buildChanges(t) {
            const changes = { inserts: [], updates: [], deletes: [] };
            t.newRows.forEach(function (row, index) {
                changes.inserts.push({ clientIndex: index, values: row.dirty });
            });
            t.rows.forEach(function (row, index) {
                if (row.deleted) {
                    changes.deletes.push({ clientIndex: index, keys: row.keys });
                } else if (Object.keys(row.dirty).length) {
                    changes.updates.push({ clientIndex: index, keys: row.keys, changes: row.dirty });
                }
            });
            return changes;
        }

        async function save(tab) {
            const t = ensure(tab);
            if (!hasChanges(tab)) {
                toast('没有需要保存的修改');
                return;
            }
            if (t.loading) {
                toast('仍在加载当前页，请稍后再保存', 'warn');
                return;
            }
            const session = sessionFor(tab);
            if (!session) {
                toast('连接已关闭，无法保存', 'error');
                return;
            }
            if (grid.commitEdit) grid.commitEdit();
            const changes = buildChanges(t);
            try {
                const preview = await api.tableApply(session.id, t.ref, changes, true);
                openPreview(tab, changes, preview || {});
            } catch (error) {
                showApplyError(tab, error);
            }
        }

        function bindPreview() {
            if (dom.tablePreviewCancel) {
                dom.tablePreviewCancel.addEventListener('click', closePreview);
            }
            if (dom.tablePreviewClose) {
                dom.tablePreviewClose.addEventListener('click', closePreview);
            }
            if (dom.tablePreview) {
                dom.tablePreview.addEventListener('click', function (event) {
                    if (event.target === dom.tablePreview) closePreview();
                });
            }
            if (dom.tablePreviewRun) {
                dom.tablePreviewRun.addEventListener('click', function () {
                    const tab = state.activeTab();
                    if (tab && tab.table && tab.table.preview) runApply(tab);
                });
            }
            document.addEventListener('keydown', function (event) {
                if (event.key === 'Escape' && dom.tablePreview && !dom.tablePreview.hidden) {
                    closePreview();
                }
            });
        }

        function openPreview(tab, changes, preview) {
            const t = ensure(tab);
            t.preview = { changes: changes, sqlPreview: preview.sqlPreview || [] };
            if (dom.tablePreviewList) {
                U.clear(dom.tablePreviewList);
                t.preview.sqlPreview.forEach(function (sql) {
                    dom.tablePreviewList.appendChild(U.el('li', 'preview-sql', sql));
                });
                if (!t.preview.sqlPreview.length) {
                    dom.tablePreviewList.appendChild(U.el('li', 'preview-empty', '没有生成 SQL'));
                }
            }
            if (dom.tablePreviewMeta) {
                const changesCount = changes.inserts.length + changes.updates.length + changes.deletes.length;
                dom.tablePreviewMeta.textContent = '新增 ' + changes.inserts.length +
                    ' · 更新 ' + changes.updates.length + ' · 删除 ' + changes.deletes.length +
                    '（共 ' + changesCount + ' 条语句）';
            }
            if (dom.tablePreviewWarn) {
                dom.tablePreviewWarn.textContent = '执行失败会整体回滚并定位到出错行。';
                dom.tablePreviewWarn.hidden = false;
            }
            if (dom.tablePreview) dom.tablePreview.hidden = false;
            if (dom.tablePreviewRun) dom.tablePreviewRun.focus();
        }

        function closePreview() {
            if (dom.tablePreview) dom.tablePreview.hidden = true;
            const tab = state.activeTab();
            if (tab && tab.table) tab.table.preview = null;
        }

        async function runApply(tab) {
            const t = ensure(tab);
            const preview = t.preview;
            if (!preview) return;
            const session = sessionFor(tab);
            if (!session) {
                closePreview();
                showApplyError(tab, { message: '连接已关闭' });
                return;
            }
            if (dom.tablePreviewRun) dom.tablePreviewRun.disabled = true;
            try {
                const result = await api.tableApply(session.id, t.ref, preview.changes, false);
                closePreview();
                t.error = null;
                t.errorRow = null;
                toast('已保存 · 影响 ' + U.formatNumber(result.affected || 0) + ' 行' +
                    (result.inTransaction ? '（通过 SAVEPOINT 保存，未提交事务）' : ''));
                if (result.warning) toast(result.warning, 'warn');
                if (ctx.tree && ctx.tree.refreshSession) {
                    // 数据变更不影响对象树结构；保留钩子以兼容后续 M6。
                }
                load(tab);
            } catch (error) {
                closePreview();
                showApplyError(tab, error);
            } finally {
                if (dom.tablePreviewRun) dom.tablePreviewRun.disabled = false;
            }
        }

        function showApplyError(tab, error) {
            const t = ensure(tab);
            const message = (error && error.message) || '保存失败';
            const detail = error && error.details ? error.details : {};
            let row = -1;
            if (typeof detail.clientIndex === 'number') {
                if (detail.kind === 'insert') row = t.rows.length + detail.clientIndex;
                else row = detail.clientIndex;
            }
            t.error = message + (detail.sqlState ? ' [' + detail.sqlState + ']' : '');
            t.errorRow = row >= 0 && row < t.display.length ? row : null;
            setError('保存失败：' + t.error +
                (t.errorRow != null ? '（第 ' + (t.errorRow + 1) + ' 行）' : ''));
            refreshGrid(tab);
            if (t.errorRow != null) {
                grid.setFocusCell(t.errorRow, 0);
                grid.focus();
            }
            toast('保存失败，已回滚：' + message, 'error');
        }

        async function truncate(tab) {
            const t = ensure(tab);
            const session = sessionFor(tab);
            if (!session) return;
            const name = t.ref && t.ref.name;
            const input = window.prompt(
                '清空表「' + name + '」将删除全部数据，且无法撤销。\n请输入表名确认：');
            if (input == null) return;
            if (input !== name) {
                toast('表名不匹配，已取消', 'warn');
                return;
            }
            try {
                const result = await api.tableTruncate(session.id, t.ref, input);
                toast('已清空表 · 影响 ' + U.formatNumber(result.affected || 0) + ' 行');
                load(tab);
            } catch (error) {
                toast('清空失败：' + ((error && error.message) || error), 'error');
            }
        }

        // ------------------------------------------------------------ 工具栏
        function bindToolbar() {
            if (dom.tableInsertBtn) {
                dom.tableInsertBtn.addEventListener('click', function () {
                    const tab = state.activeTab();
                    if (tab) addRow(tab);
                });
            }
            if (dom.tableDeleteBtn) {
                dom.tableDeleteBtn.addEventListener('click', function () {
                    const tab = state.activeTab();
                    if (tab) deleteSelected(tab);
                });
            }
            if (dom.tableRevertBtn) {
                dom.tableRevertBtn.addEventListener('click', function () {
                    const tab = state.activeTab();
                    if (tab) revert(tab);
                });
            }
            if (dom.tableSaveBtn) {
                dom.tableSaveBtn.addEventListener('click', function () {
                    const tab = state.activeTab();
                    if (tab) save(tab);
                });
            }
            if (dom.tableReloadBtn) {
                dom.tableReloadBtn.addEventListener('click', function () {
                    const tab = state.activeTab();
                    if (!tab) return;
                    if (hasChanges(tab) && !window.confirm('刷新会丢弃未保存的修改，继续？')) return;
                    reload(tab);
                });
            }
            if (dom.tableCountBtn) {
                dom.tableCountBtn.addEventListener('click', function () {
                    const tab = state.activeTab();
                    if (tab) count(tab);
                });
            }
            if (dom.tableTruncateBtn) {
                dom.tableTruncateBtn.addEventListener('click', function () {
                    const tab = state.activeTab();
                    if (tab) truncate(tab);
                });
            }
            if (dom.tablePrev) {
                dom.tablePrev.addEventListener('click', function () {
                    const tab = state.activeTab();
                    if (tab) pageBy(tab, -1);
                });
            }
            if (dom.tableNext) {
                dom.tableNext.addEventListener('click', function () {
                    const tab = state.activeTab();
                    if (tab) pageBy(tab, 1);
                });
            }
            if (dom.tablePageSize) {
                PAGE_SIZES.forEach(function (size) {
                    const option = document.createElement('option');
                    option.value = String(size);
                    option.textContent = size + ' 行/页';
                    dom.tablePageSize.appendChild(option);
                });
                dom.tablePageSize.value = '500';
                dom.tablePageSize.addEventListener('change', function () {
                    const tab = state.activeTab();
                    if (!tab) return;
                    const t = ensure(tab);
                    const nextSize = Number(dom.tablePageSize.value) || 500;
                    if (nextSize === t.pageSize) return;
                    if (hasChanges(tab) &&
                        !window.confirm('切换每页行数会丢弃当前页未保存的修改，继续？')) {
                        dom.tablePageSize.value = String(t.pageSize);
                        return;
                    }
                    t.pageSize = nextSize;
                    t.page = 0;
                    load(tab);
                });
            }
            if (dom.tableViewCellBtn) {
                dom.tableViewCellBtn.addEventListener('click', function () {
                    const tab = state.activeTab();
                    const focus = grid && grid.getFocusCell ? grid.getFocusCell() : null;
                    if (tab && focus) viewCell(tab, focus.row, focus.col);
                });
            }
        }

        // ------------------------------------------------------------ 标签接入
        function show(tab) {
            if (!tab || !dom.tablePane) return;
            dom.tablePane.hidden = false;
            const t = ensure(tab);
            if (dom.tableSearch) dom.tableSearch.value = t.search || '';
            renderFilters(tab);
            renderFilterColumns(tab);
            if (!t.loaded) {
                load(tab);
            } else if (t.gridSet && t.columns.length) {
                grid.setSet(t.gridSet);
                renderBadge(t);
                updateStatus(tab);
            } else {
                renderEmpty('没有数据', t.readOnlyReason || '调整筛选或分页条件');
                renderBadge(t);
                updateStatus(tab);
            }
            setTimeout(function () {
                if (grid) grid.focus();
            }, 0);
        }

        function close(tab) {
            if (!tab || !tab.table) return;
            tab.table.loadToken += 1;
            tab.table.preview = null;
            if (dom.tablePreview) dom.tablePreview.hidden = true;
        }

        return {
            init: init,
            show: show,
            close: close,
            save: save,
            reload: reload,
            hasChanges: hasChanges,
            load: load
        };
    }

    window.SilkitTable = { create: create };
})();
