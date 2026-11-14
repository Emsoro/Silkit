// 只读结果网格：行虚拟滚动、列宽拖拽、矩形选区、TSV 复制、单元格查看器入口。
//
//   const grid = SilkitGrid.create(host, {
//       onOpenCell: (cell) => ...,
//       onToast: (message, variant) => ...
//   });
//   grid.setSet({ columns: [...], rows: [[...]] });
//   grid.refresh();
(function () {
    'use strict';

    const U = window.SilkitUtil;

    const ROW_HEIGHT = 26;
    const ROWNO_WIDTH = 52;
    const OVERSCAN = 8;

    function create(host, options) {
        const opts = options || {};
        const state = {
            set: null,
            columns: null,
            widths: [],
            rowHeight: ROW_HEIGHT,
            scrollTop: 0,
            selection: null,
            dragAnchor: null,
            dragging: false,
            editing: null,
            emptyMessage: '还没有结果',
            emptyHint: '按 Ctrl+Enter 执行当前 SQL',
            observer: null
        };

        let root = null;
        let headRow = null;
        let canvas = null;
        let rowsLayer = null;
        let emptyBox = null;
        let emptyTitle = null;
        let emptyHint = null;
        let resize = null;
        let frame = 0;

        U.clear(host);
        host.classList.add('grid-wrap');

        const shell = document.createElement('div');
        shell.className = 'vgrid';
        shell.tabIndex = 0;
        shell.setAttribute('role', 'grid');
        shell.setAttribute('aria-label', '查询结果');
        shell.innerHTML =
            '<div class="vgrid-head" role="rowgroup">' +
            '  <div class="vgrid-headrow" role="row"></div>' +
            '</div>' +
            '<div class="vgrid-canvas" role="rowgroup">' +
            '  <div class="vgrid-rows" role="rowgroup"></div>' +
            '</div>';
        host.appendChild(shell);

        emptyBox = U.el('div', 'grid-empty');
        emptyTitle = U.el('p', 'grid-empty-title', state.emptyMessage);
        emptyHint = U.el('p', 'grid-empty-hint', state.emptyHint);
        emptyBox.appendChild(window.Icons ? U.el('span', 'grid-empty-icon') : U.el('span', 'grid-empty-icon'));
        if (window.Icons) emptyBox.firstChild.innerHTML = window.Icons.svg('grid', 28);
        emptyBox.appendChild(emptyTitle);
        emptyBox.appendChild(emptyHint);
        host.appendChild(emptyBox);

        root = shell;
        headRow = shell.querySelector('.vgrid-headrow');
        canvas = shell.querySelector('.vgrid-canvas');
        rowsLayer = shell.querySelector('.vgrid-rows');

        // ---------------------------------------------------------- 列宽
        function computeWidth(column, index) {
            const name = String(column && column.name != null ? column.name : '列 ' + (index + 1));
            const type = String(column && (column.type || column.kind) || '');
            let width = Math.max(90, name.length * 7.2 + 34);
            if (type) width = Math.max(width, Math.min(180, type.length * 6.4 + 52));
            const rows = state.set && state.set.rows ? state.set.rows : [];
            const sample = Math.min(rows.length, 60);
            for (let i = 0; i < sample; i += 1) {
                const value = rows[i][index];
                if (value == null) continue;
                const text = String(value);
                const visual = Math.min(text.length, 36);
                width = Math.max(width, visual * 6.9 + 28);
                if (width >= 420) break;
            }
            return Math.max(80, Math.min(420, Math.round(width)));
        }

        function ensureWidths() {
            const columns = state.set ? state.set.columns || [] : [];
            if (!columns.length || columns === state.columns) return;
            state.columns = columns;
            if (Array.isArray(state.set.widths) && state.set.widths.length === columns.length) {
                state.widths = state.set.widths.slice();
            } else {
                state.widths = columns.map(computeWidth);
                state.set.widths = state.widths.slice();
            }
            applyTemplate();
        }

        function applyTemplate() {
            const template = ROWNO_WIDTH + 'px ' + state.widths.map(function (w) { return w + 'px'; }).join(' ');
            const total = ROWNO_WIDTH + state.widths.reduce(function (sum, w) { return sum + w; }, 0);
            root.style.setProperty('--vgrid-cols', template.trim());
            root.style.setProperty('--vgrid-min-w', total + 'px');
            shell.style.setProperty('--vgrid-cols', template.trim());
            shell.style.setProperty('--vgrid-min-w', total + 'px');
            if (headRow) {
                headRow.style.gridTemplateColumns = template;
                headRow.style.minWidth = total + 'px';
            }
            if (rowsLayer) rowsLayer.style.minWidth = total + 'px';
        }

        function autoFit(index) {
            const columns = state.set && state.set.columns ? state.set.columns : [];
            const column = columns[index];
            if (!column) return;
            const rows = state.set.rows || [];
            let width = Math.max(90, String(column.name || '').length * 7.2 + 34);
            const sample = Math.min(rows.length, 400);
            for (let i = 0; i < sample; i += 1) {
                const value = rows[i][index];
                if (value == null) continue;
                width = Math.max(width, Math.min(600, String(value).length * 6.9 + 30));
            }
            state.widths[index] = Math.round(Math.min(600, width));
            if (state.set) state.set.widths = state.widths.slice();
            applyTemplate();
            renderRows();
        }

        // ---------------------------------------------------------- 表头
        function renderHead() {
            const columns = state.set && state.set.columns ? state.set.columns : [];
            const sort = opts.sortState ? opts.sortState() : null;
            U.clear(headRow);

            const corner = U.el('div', 'vgrid-hcell vgrid-rownum', '#');
            corner.setAttribute('role', 'columnheader');
            headRow.appendChild(corner);

            columns.forEach(function (column, index) {
                const cell = U.el('div', 'vgrid-hcell');
                cell.setAttribute('role', 'columnheader');
                cell.setAttribute('data-col', String(index));
                if (sort && sort.column === column.name) {
                    cell.classList.add('is-sorted');
                }
                const name = U.el('span', 'vgrid-hname', column.name || ('列 ' + (index + 1)));
                const type = U.el('span', 'vgrid-htype', column.type || column.kind || '');
                if (column.type) {
                    cell.title = column.name + ' · ' + column.type +
                        (column.primaryKey ? ' · 主键' : '') +
                        (column.nullable === false ? ' · NOT NULL' : '');
                }
                cell.appendChild(name);
                if (column.type || column.kind) cell.appendChild(type);
                if (sort && sort.column === column.name) {
                    cell.appendChild(U.el('span', 'vgrid-sort', sort.desc ? '↓' : '↑'));
                }
                const handle = U.el('span', 'vgrid-resize');
                handle.setAttribute('data-col', String(index));
                handle.title = '拖拽调整列宽 · 双击自动适应';
                cell.appendChild(handle);
                headRow.appendChild(cell);
            });

            applyTemplate();
        }

        // ---------------------------------------------------------- 行
        function rowStart() {
            const visible = host.clientHeight || 400;
            const first = Math.max(0, Math.floor(state.scrollTop / state.rowHeight) - OVERSCAN);
            const count = Math.ceil(visible / state.rowHeight) + OVERSCAN * 2;
            return { first: first, count: count, total: currentRows().length };
        }

        function currentRows() {
            return state.set && Array.isArray(state.set.rows) ? state.set.rows : [];
        }

        function cellContent(column, value, rowIndex, colIndex) {
            const cell = U.el('div', 'vgrid-cell');
            cell.setAttribute('role', 'gridcell');
            cell.setAttribute('data-row', String(rowIndex));
            cell.setAttribute('data-col', String(colIndex));

            if (opts.cellClass) {
                const extra = opts.cellClass(rowIndex, colIndex);
                if (extra) extra.split(/\s+/).forEach(function (name) {
                    if (name) cell.classList.add(name);
                });
            }

            if (value === undefined) {
                cell.classList.add('is-unset');
                return cell;
            }

            const kind = String(column && column.kind || '').toLowerCase();
            if (value === null || value === undefined) {
                cell.classList.add('is-null');
                const tag = U.el('span', 'cell-null', 'NULL');
                cell.appendChild(tag);
                cell.title = 'NULL';
                return cell;
            }

            if (kind === 'binary') {
                cell.classList.add('is-blob');
                const bytes = U.base64Bytes(value);
                const text = U.el('span', 'cell-blob', 'BLOB · ' + U.formatBytes(bytes));
                cell.appendChild(text);
                cell.title = '二进制数据（双击查看 Base64 / 十六进制）';
                return cell;
            }

            if (value === '') {
                cell.classList.add('is-empty');
                const tag = U.el('span', 'cell-empty', "''");
                cell.appendChild(tag);
                cell.title = '空字符串';
                return cell;
            }

            if (kind === 'int' || kind === 'uint' || kind === 'float' ||
                kind === 'decimal' || kind === 'bool') {
                cell.classList.add('is-num');
            } else if (kind === 'date' || kind === 'time' || kind === 'datetime' ||
                kind === 'timestamp') {
                cell.classList.add('is-date');
            } else if (kind === 'json') {
                cell.classList.add('is-json');
            }

            const text = String(value).replace(/\s+/g, ' ');
            const preview = text.length > 480 ? text.slice(0, 480) + '…' : text;
            cell.appendChild(document.createTextNode(preview));
            cell.title = text.length > 480 ? '内容较长，双击查看完整值' : String(value);
            return cell;
        }

        function renderRows() {
            frame = 0;
            const rows = currentRows();
            const columns = state.set && state.set.columns ? state.set.columns : [];
            if (!root || !rowsLayer) return;
            if (!columns.length) {
                U.clear(rowsLayer);
                canvas.style.height = '0px';
                return;
            }

            const span = rowStart();
            const end = Math.min(span.total, span.first + span.count);
            const fragment = document.createDocumentFragment();
            const template = ROWNO_WIDTH + 'px ' + state.widths.map(function (w) { return w + 'px'; }).join(' ');
            const selection = normalizedSelection();
            let lastUsed = -1;

            for (let rowIndex = span.first; rowIndex < end; rowIndex += 1) {
                const row = rows[rowIndex] || [];
                const node = document.createElement('div');
                node.className = 'vgrid-row' + (rowIndex % 2 === 1 ? ' is-even' : '');
                node.setAttribute('role', 'row');
                node.setAttribute('data-row', String(rowIndex));
                if (opts.rowClass) {
                    const extra = opts.rowClass(rowIndex);
                    if (extra) extra.split(/\s+/).forEach(function (name) {
                        if (name) node.classList.add(name);
                    });
                }
                node.style.gridTemplateColumns = template;
                node.style.minWidth = (ROWNO_WIDTH + state.widths.reduce(function (s, w) { return s + w; }, 0)) + 'px';
                U.clear(node);

                const rowNo = U.el('div', 'vgrid-cell vgrid-rownum', String(rowIndex + 1));
                rowNo.setAttribute('data-row', String(rowIndex));
                rowNo.setAttribute('data-col', '-1');
                if (selection && rowIndex >= selection.r1 && rowIndex <= selection.r2) {
                    rowNo.classList.add('is-selected');
                }
                node.appendChild(rowNo);

                for (let col = 0; col < columns.length; col += 1) {
                    const cell = cellContent(columns[col], row[col], rowIndex, col);
                    if (selection && rowIndex >= selection.r1 && rowIndex <= selection.r2 &&
                        col >= selection.c1 && col <= selection.c2) {
                        cell.classList.add('is-selected');
                    }
                    if (selection && selection.fr === rowIndex && selection.fc === col) {
                        cell.classList.add('is-focus');
                    }
                    node.appendChild(cell);
                }
                fragment.appendChild(node);
                lastUsed = rowIndex;
            }

            U.clear(rowsLayer);
            rowsLayer.style.top = (span.first * state.rowHeight) + 'px';
            rowsLayer.appendChild(fragment);
            canvas.style.height = Math.max(0, rows.length * state.rowHeight) + 'px';
            if (lastUsed < span.first) U.clear(rowsLayer);
        }

        function scheduleRows() {
            if (frame) return;
            frame = requestAnimationFrame(renderRows);
        }

        // ---------------------------------------------------------- 选区
        function normalizedSelection() {
            const sel = state.selection;
            if (!sel) return null;
            return {
                r1: Math.min(sel.anchor.r, sel.focus.r),
                r2: Math.max(sel.anchor.r, sel.focus.r),
                c1: Math.min(sel.anchor.c, sel.focus.c),
                c2: Math.max(sel.anchor.c, sel.focus.c),
                fr: sel.focus.r,
                fc: sel.focus.c
            };
        }

        function cellAt(target) {
            const cell = target && target.closest ? target.closest('.vgrid-cell') : null;
            if (!cell || !host.contains(cell)) return null;
            const row = Number(cell.getAttribute('data-row'));
            const col = Number(cell.getAttribute('data-col'));
            if (!Number.isFinite(row) || row < 0) return null;
            return { row: row, col: col, cell: cell };
        }

        function setSelection(anchor, focus) {
            state.selection = { anchor: anchor, focus: focus };
            renderRows();
        }

        function selectAll() {
            const rows = currentRows();
            const columns = state.set && state.set.columns ? state.set.columns : [];
            if (!rows.length || !columns.length) return;
            setSelection({ row: 0, col: 0 }, { row: rows.length - 1, col: columns.length - 1 });
        }

        function clearSelection() {
            state.selection = null;
            renderRows();
        }

        function selectionText() {
            const rows = currentRows();
            const sel = normalizedSelection();
            if (!rows.length) return '';
            const range = sel || {
                r1: 0, r2: rows.length - 1,
                c1: 0, c2: (state.set.columns || []).length - 1
            };
            const lines = [];
            for (let r = range.r1; r <= range.r2; r += 1) {
                const row = rows[r] || [];
                const values = [];
                for (let c = range.c1; c <= range.c2; c += 1) {
                    const value = row[c];
                    values.push(value == null ? '' : String(value)
                        .replace(/\t/g, ' ')
                        .replace(/\r?\n/g, '\\n'));
                }
                lines.push(values.join('\t'));
            }
            return lines.join('\n');
        }

        function copySelection() {
            const text = selectionText();
            if (!text) {
                if (opts.onToast) opts.onToast('没有可复制的内容', 'error');
                return;
            }
            U.copyText(text).then(function () {
                if (opts.onToast) opts.onToast('已复制 ' + (text.split('\n').length) + ' 行 TSV');
            }).catch(function (error) {
                if (opts.onToast) opts.onToast('复制失败：' + (error.message || error), 'error');
            });
        }

        function copyAll(includeHeader) {
            const rows = currentRows();
            if (!rows.length) {
                if (opts.onToast) opts.onToast('结果为空，没有可复制的内容', 'error');
                return;
            }
            const columns = state.set.columns || [];
            const lines = [];
            if (includeHeader) {
                lines.push(columns.map(function (column) { return column.name || ''; }).join('\t'));
            }
            for (let r = 0; r < rows.length; r += 1) {
                lines.push(rows[r].map(function (value) {
                    return value == null ? '' : String(value)
                        .replace(/\t/g, ' ')
                        .replace(/\r?\n/g, '\\n');
                }).join('\t'));
            }
            const text = lines.join('\n');
            U.copyText(text).then(function () {
                if (opts.onToast) opts.onToast('已复制 ' + rows.length + ' 行（含表头）TSV');
            }).catch(function (error) {
                if (opts.onToast) opts.onToast('复制失败：' + (error.message || error), 'error');
            });
        }

        function openCell(rowIndex, colIndex) {
            if (!opts.onOpenCell) return;
            const columns = state.set && state.set.columns ? state.set.columns : [];
            const column = columns[colIndex] || { name: '列 ' + (colIndex + 1) };
            const rows = currentRows();
            const value = rows[rowIndex] ? rows[rowIndex][colIndex] : null;
            opts.onOpenCell({
                column: column,
                value: value,
                rowIndex: rowIndex,
                colIndex: colIndex,
                row: rows[rowIndex] || []
            });
        }

        // ---------------------------------------------------------- 单元格编辑
        function canEditCell(rowIndex, colIndex) {
            return !!(opts.canEdit && opts.canEdit(rowIndex, colIndex));
        }

        function stopEditing(commit, ed) {
            const editing = ed || state.editing;
            if (!editing || state.editing !== editing) return;
            state.editing = null;
            if (editing.input && editing.input.parentNode) editing.input.parentNode.removeChild(editing.input);
            if (!commit) return;
            const text = editing.input ? editing.input.value : '';
            const originalText = editing.original == null ? null : String(editing.original);
            if (originalText === text) return;
            if (opts.onEditCell) opts.onEditCell(editing.row, editing.col, text, false);
        }

        function cancelEdit() {
            stopEditing(false, state.editing);
        }

        function commitEdit() {
            stopEditing(true, state.editing);
        }

        function beginEdit(rowIndex, colIndex) {
            if (!canEditCell(rowIndex, colIndex)) {
                if (opts.onToast) opts.onToast('该单元格不可编辑', 'warn');
                return false;
            }
            if (state.editing) {
                const current = state.editing;
                if (current.row === rowIndex && current.col === colIndex) {
                    if (current.input) current.input.focus();
                    return true;
                }
                commitEdit();
            }
            const rows = currentRows();
            if (!rows[rowIndex]) return false;
            const cellEl = shell.querySelector(
                '.vgrid-cell[data-row="' + rowIndex + '"][data-col="' + colIndex + '"]');
            if (!cellEl) return false;

            const input = document.createElement('input');
            input.className = 'vgrid-editor';
            input.type = 'text';
            input.autocomplete = 'off';
            input.spellcheck = false;
            const editing = {
                row: rowIndex,
                col: colIndex,
                original: rows[rowIndex][colIndex],
                input: input
            };
            input.value = editing.original == null ? '' : String(editing.original);

            const shellRect = shell.getBoundingClientRect();
            const rect = cellEl.getBoundingClientRect();
            input.style.left = (rect.left - shellRect.left) + 'px';
            input.style.top = (rect.top - shellRect.top) + 'px';
            input.style.width = Math.max(56, rect.width) + 'px';
            input.style.height = Math.max(20, rect.height) + 'px';

            input.addEventListener('keydown', function (event) {
                if (state.editing !== editing) return;
                if (event.key === 'Escape') {
                    event.preventDefault();
                    cancelEdit();
                    shell.focus({ preventScroll: true });
                    return;
                }
                if (event.key === 'Enter') {
                    event.preventDefault();
                    commitEdit();
                    shell.focus({ preventScroll: true });
                    return;
                }
                if (event.key === 'Tab') {
                    event.preventDefault();
                    const columns = state.set && state.set.columns ? state.set.columns : [];
                    const next = colIndex + (event.shiftKey ? -1 : 1);
                    commitEdit();
                    if (next >= 0 && next < columns.length) beginEdit(rowIndex, next);
                    else shell.focus({ preventScroll: true });
                    return;
                }
                if ((event.ctrlKey || event.metaKey) && event.key === '0') {
                    event.preventDefault();
                    const target = state.editing;
                    state.editing = null;
                    if (target && target.input && target.input.parentNode) {
                        target.input.parentNode.removeChild(target.input);
                    }
                    if (opts.onEditCell) opts.onEditCell(rowIndex, colIndex, null, true);
                    shell.focus({ preventScroll: true });
                }
            });
            input.addEventListener('blur', function () {
                if (state.editing === editing) commitEdit();
            });

            state.editing = editing;
            shell.appendChild(input);
            input.focus();
            input.select();
            return true;
        }

        function setNullAt(rowIndex, colIndex) {
            if (!canEditCell(rowIndex, colIndex)) {
                if (opts.onToast) opts.onToast('该单元格不可编辑', 'warn');
                return false;
            }
            if (opts.onEditCell) opts.onEditCell(rowIndex, colIndex, null, true);
            return true;
        }

        // ---------------------------------------------------------- 事件
        host.addEventListener('scroll', function () {
            state.scrollTop = host.scrollTop;
            scheduleRows();
        }, { passive: true });

        shell.addEventListener('mousedown', function (event) {
            const handle = event.target.closest('.vgrid-resize');
            if (handle) {
                event.preventDefault();
                event.stopPropagation();
                const index = Number(handle.getAttribute('data-col'));
                const startX = event.clientX;
                const startWidth = state.widths[index] || 120;
                function move(moveEvent) {
                    state.widths[index] = Math.max(56, Math.min(900, startWidth + moveEvent.clientX - startX));
                    if (state.set) state.set.widths = state.widths.slice();
                    applyTemplate();
                }
                function up() {
                    document.removeEventListener('mousemove', move);
                    document.removeEventListener('mouseup', up);
                    document.body.classList.remove('is-col-resizing');
                    renderRows();
                }
                document.body.classList.add('is-col-resizing');
                document.addEventListener('mousemove', move);
                document.addEventListener('mouseup', up);
                return;
            }

            const header = event.target.closest('.vgrid-hcell[data-col]');
            if (header) {
                const index = Number(header.getAttribute('data-col'));
                if (opts.onSortColumn) {
                    event.preventDefault();
                    const columns = state.set && state.set.columns ? state.set.columns : [];
                    if (columns[index]) opts.onSortColumn(index, columns[index]);
                    return;
                }
                const rows = currentRows();
                if (rows.length) {
                    setSelection({ row: 0, col: index }, { row: rows.length - 1, col: index });
                }
                return;
            }

            const target = cellAt(event.target);
            if (!target) return;

            if (target.col < 0) {
                const columns = state.set && state.set.columns ? state.set.columns : [];
                if (columns.length) {
                    setSelection({ row: target.row, col: 0 },
                        { row: target.row, col: columns.length - 1 });
                }
                return;
            }

            shell.focus({ preventScroll: true });
            if (event.shiftKey && state.selection) {
                state.selection.focus = { row: target.row, col: target.col };
                renderRows();
            } else {
                setSelection({ row: target.row, col: target.col },
                    { row: target.row, col: target.col });
            }
            state.dragAnchor = { row: target.row, col: target.col };
            state.dragging = true;
            event.preventDefault();
        });

        shell.addEventListener('mouseover', function (event) {
            if (!state.dragging || !state.selection) return;
            const target = cellAt(event.target);
            if (!target || target.col < 0) return;
            const focus = state.selection.focus;
            if (focus.row === target.row && focus.col === target.col) return;
            state.selection.focus = { row: target.row, col: target.col };
            renderRows();
        });

        document.addEventListener('mouseup', function () {
            if (!state.dragging) return;
            state.dragging = false;
            state.dragAnchor = null;
        });

        shell.addEventListener('dblclick', function (event) {
            const handle = event.target.closest('.vgrid-resize');
            if (handle) {
                autoFit(Number(handle.getAttribute('data-col')));
                return;
            }
            const header = event.target.closest('.vgrid-hcell[data-col]');
            if (header) {
                autoFit(Number(header.getAttribute('data-col')));
                return;
            }
            const target = cellAt(event.target);
            if (!target || target.col < 0) return;
            if (canEditCell(target.row, target.col)) {
                beginEdit(target.row, target.col);
                return;
            }
            openCell(target.row, target.col);
        });

        shell.addEventListener('keydown', function (event) {
            if (state.editing) return;
            const mod = event.ctrlKey || event.metaKey;
            if (mod && event.code === 'KeyC') {
                event.preventDefault();
                copySelection();
                return;
            }
            if (mod && event.code === 'KeyA') {
                event.preventDefault();
                selectAll();
                return;
            }
            if (event.key === 'Escape') {
                clearSelection();
                return;
            }
            if (!state.selection) return;
            const rows = currentRows();
            const columns = state.set && state.set.columns ? state.set.columns : [];
            if (!rows.length || !columns.length) return;
            let row = state.selection.focus.row;
            let col = state.selection.focus.col;
            const page = Math.max(1, Math.floor(host.clientHeight / state.rowHeight) - 2);
            if (event.key === 'F2' || event.key === 'Enter') {
                event.preventDefault();
                if (canEditCell(row, col)) beginEdit(row, col);
                return;
            }
            if (mod && event.key === '0') {
                event.preventDefault();
                setNullAt(row, col);
                return;
            }
            if (event.key === 'ArrowDown') row += 1;
            else if (event.key === 'ArrowUp') row -= 1;
            else if (event.key === 'ArrowRight') col += 1;
            else if (event.key === 'ArrowLeft') col -= 1;
            else if (event.key === 'PageDown') row += page;
            else if (event.key === 'PageUp') row -= page;
            else if (event.key === 'Home') col = 0;
            else if (event.key === 'End') col = columns.length - 1;
            else return;
            event.preventDefault();
            row = Math.max(0, Math.min(rows.length - 1, row));
            col = Math.max(0, Math.min(columns.length - 1, col));
            if (event.shiftKey) {
                state.selection.focus = { row: row, col: col };
                renderRows();
            } else {
                setSelection({ row: row, col: col }, { row: row, col: col });
            }
            const rowTop = row * state.rowHeight;
            if (rowTop < host.scrollTop) host.scrollTop = rowTop;
            else if (rowTop + state.rowHeight > host.scrollTop + host.clientHeight) {
                host.scrollTop = rowTop + state.rowHeight - host.clientHeight;
            }
        });

        // ---------------------------------------------------------- 对外 API
        function setEmpty(message, hint) {
            state.emptyMessage = message || '还没有结果';
            state.emptyHint = hint || '按 Ctrl+Enter 执行当前 SQL';
            if (emptyTitle) emptyTitle.textContent = state.emptyMessage;
            if (emptyHint) emptyHint.textContent = state.emptyHint;
            emptyBox.hidden = false;
            shell.hidden = true;
        }

        function setSet(set) {
            cancelEdit();
            const changed = set !== state.set;
            state.set = set || null;
            state.columns = null;
            state.selection = null;
            state.scrollTop = 0;
            if (changed && host) host.scrollTop = 0;

            const columns = set && set.columns ? set.columns : [];
            const rows = set && set.rows ? set.rows : [];
            if (!columns.length) {
                setEmpty(set && set.message ? set.message : state.emptyMessage,
                    set && set.hint ? set.hint : state.emptyHint);
                return;
            }
            emptyBox.hidden = true;
            shell.hidden = false;
            ensureWidths();
            renderHead();
            renderRows();
            if (set.autoScrollEnd) host.scrollTop = host.scrollHeight;
        }

        function refresh() {
            if (!state.set) return;
            const columns = state.set.columns || [];
            if (!columns.length) {
                setEmpty(state.emptyMessage, state.emptyHint);
                return;
            }
            emptyBox.hidden = true;
            shell.hidden = false;
            if (state.columns !== columns) {
                ensureWidths();
                renderHead();
            }
            scheduleRows();
        }

        function focus() {
            if (shell) shell.focus({ preventScroll: true });
        }

        function destroy() {
            if (state.observer) state.observer.disconnect();
            if (frame) cancelAnimationFrame(frame);
            U.clear(host);
        }

        if (window.ResizeObserver) {
            state.observer = new ResizeObserver(function () { scheduleRows(); });
            state.observer.observe(host);
        }

        setEmpty(state.emptyMessage, state.emptyHint);

        return {
            setSet: setSet,
            refresh: refresh,
            setEmpty: setEmpty,
            focus: focus,
            destroy: destroy,
            copyAll: copyAll,
            copySelection: copySelection,
            selectAll: selectAll,
            clearSelection: clearSelection,
            getSelectionText: selectionText,
            getSelection: normalizedSelection,
            getFocusCell: function () {
                return state.selection ? {
                    row: state.selection.focus.row,
                    col: state.selection.focus.col
                } : null;
            },
            setFocusCell: function (row, col) {
                const rows = currentRows();
                const columns = state.set && state.set.columns ? state.set.columns : [];
                if (!rows.length || !columns.length) return;
                const r = Math.max(0, Math.min(rows.length - 1, Number(row) || 0));
                const c = Math.max(0, Math.min(columns.length - 1, Number(col) || 0));
                setSelection({ row: r, col: c }, { row: r, col: c });
                const top = r * state.rowHeight;
                if (top < host.scrollTop) host.scrollTop = top;
                else if (top + state.rowHeight > host.scrollTop + host.clientHeight) {
                    host.scrollTop = top + state.rowHeight - host.clientHeight;
                }
            },
            selectedRows: function () {
                const sel = normalizedSelection();
                if (!sel) return [];
                const out = [];
                for (let r = sel.r1; r <= sel.r2; r += 1) out.push(r);
                return out;
            },
            beginEdit: beginEdit,
            commitEdit: commitEdit,
            cancelEdit: cancelEdit,
            isEditing: function () { return !!state.editing; },
            rowCount: function () { return currentRows().length; }
        };
    }

    window.SilkitGrid = { create: create, ROW_HEIGHT: ROW_HEIGHT };
})();
