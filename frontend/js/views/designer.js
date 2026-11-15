// M6 表设计器：列/索引/外键/选项编辑，DDL 预览、令牌确认与执行错误恢复。
// 另外提供删除表/清空表/新建库/删除库的统一 SQL 预览确认框。
(function () {
    'use strict';

    const U = window.SilkitUtil;

    const ACTIONS = ['NO ACTION', 'CASCADE', 'SET NULL', 'SET DEFAULT', 'RESTRICT'];

    function create(ctx) {
        const api = ctx.api;
        const toast = ctx.toast;

        const dom = {};
        let current = null;
        let confirmState = null;
        let busy = false;

        function byId(id) { return document.getElementById(id); }

        function init() {
            ['designerDialog', 'designerTitle', 'designerSub', 'designerDirty', 'designerClose',
                'designerError', 'designerAddColumn', 'designerAddIndex', 'designerAddForeignKey',
                'designerColumns', 'designerIndexes', 'designerForeignKeys',
                'designerTypeList', 'designerColumnList', 'designerTableList',
                'designerTableName', 'designerScope', 'designerEngine', 'designerCharset',
                'designerCollation', 'designerComment', 'designerOptionHint',
                'designerPreview', 'designerPreview2', 'designerApply', 'designerCancel',
                'designerSql', 'designerWarnings', 'designerImpact', 'designerFootNote',
                'ddlConfirm', 'ddlConfirmTitle', 'ddlConfirmSub', 'ddlConfirmSql',
                'ddlConfirmWarn', 'ddlConfirmNameField', 'ddlConfirmName',
                'ddlConfirmError', 'ddlConfirmNote', 'ddlConfirmClose', 'ddlConfirmCancel',
                'ddlConfirmRun'
            ].forEach(function (id) { dom[id] = byId(id); });
            if (!dom.designerDialog) return;

            dom.designerClose.addEventListener('click', requestClose);
            dom.designerCancel.addEventListener('click', requestClose);
            dom.designerDialog.addEventListener('click', function (event) {
                if (event.target === dom.designerDialog) requestClose();
            });
            dom.designerPreview.addEventListener('click', preview);
            dom.designerPreview2.addEventListener('click', preview);
            dom.designerApply.addEventListener('click', apply);
            dom.designerAddColumn.addEventListener('click', addColumn);
            dom.designerAddIndex.addEventListener('click', addIndex);
            dom.designerAddForeignKey.addEventListener('click', addForeignKey);

            U.qsa('.designer-tab', dom.designerDialog).forEach(function (tab) {
                tab.addEventListener('click', function () {
                    showPanel(tab.getAttribute('data-panel'));
                });
            });

            bindOption(dom.designerTableName, function (value) {
                current.draft.name = value;
            });
            bindOption(dom.designerEngine, function (value) {
                current.draft.options.engine = value;
            });
            bindOption(dom.designerCharset, function (value) {
                current.draft.options.charset = value;
            });
            bindOption(dom.designerCollation, function (value) {
                current.draft.options.collation = value;
            });
            bindOption(dom.designerComment, function (value) {
                current.draft.options.comment = value;
            });

            dom.ddlConfirmClose.addEventListener('click', closeConfirm);
            dom.ddlConfirmCancel.addEventListener('click', closeConfirm);
            dom.ddlConfirm.addEventListener('click', function (event) {
                if (event.target === dom.ddlConfirm) closeConfirm();
            });
            dom.ddlConfirmName.addEventListener('input', updateConfirmRunState);
            dom.ddlConfirmRun.addEventListener('click', runConfirm);

            document.addEventListener('keydown', function (event) {
                if (event.key !== 'Escape') return;
                if (dom.ddlConfirm && !dom.ddlConfirm.hidden) {
                    closeConfirm();
                } else if (dom.designerDialog && !dom.designerDialog.hidden) {
                    requestClose();
                }
            });
        }

        function bindOption(input, applyValue) {
            if (!input) return;
            input.addEventListener('input', function () {
                if (!current) return;
                applyValue(input.value);
                changed();
            });
        }

        function sessionEngine(session) {
            const profile = (session && session.profile) || {};
            return profile.engine || profile.driver || (session && session.engine) || 'sqlite';
        }

        function engineLabel(engine) {
            if (engine === 'mysql') return 'MySQL';
            if (engine === 'postgres' || engine === 'postgresql') return 'PostgreSQL';
            return 'SQLite';
        }

        function clone(value) { return JSON.parse(JSON.stringify(value)); }

        function scopeOf(ref, engine) {
            if ((engine || '') === 'postgres' || (engine || '') === 'postgresql') {
                return (ref && ref.schema) || 'public';
            }
            return (ref && ref.database) || 'main';
        }

        function blankDefinition(engine, scope) {
            const type = engine === 'mysql' ? 'INT' : (engine === 'postgres' ? 'integer' : 'INTEGER');
            const definition = {
                engine: engine,
                scope: scope,
                name: '',
                exists: false,
                primaryKeyName: '',
                columns: [{
                    name: 'id', type: type, notNull: true, primaryKey: true,
                    autoIncrement: true, hasDefault: false, default: '', comment: '',
                    oldName: '', _orig: ''
                }],
                indexes: [],
                foreignKeys: [],
                options: {
                    engine: engine === 'mysql' ? 'InnoDB' : '',
                    charset: engine === 'mysql' ? 'utf8mb4' : '',
                    collation: engine === 'mysql' ? 'utf8mb4_general_ci' : '',
                    comment: ''
                }
            };
            return definition;
        }

        function payload() {
            const draft = current.draft;
            return {
                name: String(draft.name || '').trim(),
                scope: draft.scope,
                primaryKeyName: draft.primaryKeyName || '',
                columns: draft.columns.map(function (column) {
                    return {
                        name: String(column.name || '').trim(),
                        type: String(column.type || '').trim(),
                        notNull: !!column.notNull,
                        primaryKey: !!column.primaryKey,
                        autoIncrement: !!column.autoIncrement,
                        hasDefault: !!column.hasDefault && String(column.default || '').trim() !== '',
                        default: String(column.default || ''),
                        comment: String(column.comment || ''),
                        oldName: column.oldName || ''
                    };
                }),
                indexes: draft.indexes.map(function (index) {
                    return {
                        name: String(index.name || '').trim(),
                        unique: !!index.unique,
                        columns: (index.columns || []).filter(Boolean)
                    };
                }),
                foreignKeys: draft.foreignKeys.map(function (fk) {
                    return {
                        name: String(fk.name || '').trim(),
                        columns: (fk.columns || []).filter(Boolean),
                        refSchema: '',
                        refTable: String(fk.refTable || '').trim(),
                        refColumns: (fk.refColumns || []).filter(Boolean),
                        onUpdate: fk.onUpdate || 'NO ACTION',
                        onDelete: fk.onDelete || 'NO ACTION'
                    };
                }),
                options: {
                    engine: String(draft.options.engine || ''),
                    charset: String(draft.options.charset || ''),
                    collation: String(draft.options.collation || ''),
                    comment: String(draft.options.comment || '')
                }
            };
        }

        function tableRef() {
            const engine = current.engine;
            const scope = current.draft ? current.draft.scope : scopeOf(current.ref, engine);
            const postgres = engine === 'postgres' || engine === 'postgresql';
            return {
                kind: 'table',
                name: current.originalName || '',
                database: postgres ? '' : scope,
                schema: postgres ? scope : ''
            };
        }

        function showDialog() {
            dom.designerDialog.hidden = false;
            dom.designerClose.focus();
        }

        function setLoading(loading, message) {
            dom.designerSub.textContent = message || '';
            if (loading) {
                dom.designerSql.textContent = '正在读取元数据…';
                dom.designerApply.disabled = true;
            }
        }

        function showError(message) {
            dom.designerError.textContent = message;
            dom.designerError.hidden = false;
        }

        function clearError() {
            dom.designerError.hidden = true;
            dom.designerError.textContent = '';
        }

        async function openTableDesigner(session, ref) {
            if (!session) return;
            const target = ref || {};
            current = {
                session: session,
                ref: target,
                mode: target.name ? 'alter' : 'create',
                originalName: target.name || '',
                engine: sessionEngine(session),
                types: [],
                supports: {},
                tables: [],
                draft: null,
                preview: null,
                token: null,
                snapshot: ''
            };
            clearError();
            dom.designerApply.disabled = true;
            dom.designerPreview.disabled = false;
            showDialog();
            setLoading(true, '读取表定义…');

            try {
                const meta = await api.ddlTypes({ sessionId: session.id });
                current.engine = meta.engine || current.engine;
                current.types = meta.types || [];
                current.supports = meta.supports || {};

                if (current.mode === 'alter') {
                    const loaded = await api.ddlLoad(session.id, {
                        kind: 'table',
                        name: current.originalName,
                        database: current.engine === 'postgres' ? '' : scopeOf(target, current.engine),
                        schema: current.engine === 'postgres' ? scopeOf(target, current.engine) : ''
                    });
                    current.original = loaded.definition;
                    current.originalName = loaded.definition.name;
                    current.draft = clone(loaded.definition);
                    current.draft.columns.forEach(function (column) {
                        column._orig = column.name;
                        column.oldName = '';
                    });
                } else {
                    current.draft = blankDefinition(current.engine, scopeOf(target, current.engine));
                }

                current.snapshot = JSON.stringify(payload());
                try {
                    const tables = await api.ddlTables(session.id, tableRef());
                    current.tables = tables.tables || [];
                } catch (error) {
                    current.tables = [];
                }
                render();
            } catch (error) {
                showError('读取表定义失败：' + errorText(error));
                dom.designerSub.textContent = '无法加载，请关闭后重试';
            }
        }

        function errorText(error) {
            return (error && (error.message || error.detail || error.code)) || String(error);
        }

        function render() {
            const engine = current.engine;
            const isPostgres = engine === 'postgres' || engine === 'postgresql';
            const scopeLabel = isPostgres ? 'Schema' : (engine === 'sqlite' ? '附加库' : '数据库');
            dom.designerTitle.textContent = (current.mode === 'create' ? '新建表' : '设计表') +
                (current.mode === 'create' ? '' : ' · ' + current.originalName);
            dom.designerSub.textContent = engineLabel(engine) + ' · ' + scopeLabel + ' ' +
                current.draft.scope + (current.mode === 'alter' ? ' · 修改现有表' : ' · 尚未创建');
            dom.designerScope.value = current.draft.scope;
            dom.designerTableName.value = current.draft.name || '';
            dom.designerEngine.value = current.draft.options.engine || '';
            dom.designerCharset.value = current.draft.options.charset || '';
            dom.designerCollation.value = current.draft.options.collation || '';
            dom.designerComment.value = current.draft.options.comment || '';

            const supports = current.supports || {};
            dom.designerEngine.disabled = !supports.storageOptions;
            dom.designerCharset.disabled = !supports.storageOptions;
            dom.designerCollation.disabled = !supports.storageOptions;
            dom.designerComment.disabled = !supports.tableComment;
            dom.designerOptionHint.textContent = supports.storageOptions
                ? 'MySQL 支持存储引擎、字符集与排序规则。'
                : (supports.tableComment ? 'PostgreSQL 支持表注释；字符集由数据库决定。'
                    : 'SQLite 不支持表注释、字符集与存储引擎。');

            fillTypeList();
            fillColumnList();
            fillTableList();
            renderColumns();
            renderIndexes();
            renderForeignKeys();

            dom.designerSql.textContent = '尚未生成 SQL。点击底部「预览 SQL」。';
            renderWarnings([]);
            renderImpact('');
            updateDirty();
            showPanel('columns');
        }

        function fillTypeList() {
            U.clear(dom.designerTypeList);
            (current.types || []).forEach(function (type) {
                const option = document.createElement('option');
                option.value = type;
                dom.designerTypeList.appendChild(option);
            });
        }

        function fillColumnList() {
            U.clear(dom.designerColumnList);
            current.draft.columns.forEach(function (column) {
                if (!column.name) return;
                const option = document.createElement('option');
                option.value = column.name;
                dom.designerColumnList.appendChild(option);
            });
        }

        function fillTableList() {
            U.clear(dom.designerTableList);
            (current.tables || []).forEach(function (name) {
                const option = document.createElement('option');
                option.value = name;
                dom.designerTableList.appendChild(option);
            });
        }

        function showPanel(name) {
            U.qsa('.designer-tab', dom.designerDialog).forEach(function (tab) {
                tab.classList.toggle('is-active', tab.getAttribute('data-panel') === name);
            });
            U.qsa('.designer-panel', dom.designerDialog).forEach(function (panel) {
                panel.hidden = panel.getAttribute('data-panel') !== name;
            });
            if (name === 'columns') fillColumnList();
        }

        function textInput(value, onInput, options) {
            const opts = options || {};
            const input = document.createElement('input');
            input.className = 'designer-input';
            input.type = 'text';
            input.value = value == null ? '' : String(value);
            input.autocomplete = 'off';
            input.spellcheck = false;
            if (opts.list) input.setAttribute('list', opts.list);
            if (opts.placeholder) input.placeholder = opts.placeholder;
            if (opts.disabled) {
                input.disabled = true;
                if (opts.title) input.title = opts.title;
            }
            input.addEventListener('input', function () { onInput(input.value); });
            return input;
        }

        function checkbox(checked, onChange, disabled) {
            const input = document.createElement('input');
            input.type = 'checkbox';
            input.className = 'designer-check';
            input.checked = !!checked;
            input.disabled = !!disabled;
            input.addEventListener('change', function () { onChange(input.checked); });
            return input;
        }

        function cell(content, className) {
            const td = document.createElement('td');
            if (className) td.className = className;
            if (content instanceof Node) td.appendChild(content);
            else td.textContent = content == null ? '' : String(content);
            return td;
        }

        function rowOps(index, list, rerender) {
            const box = document.createElement('div');
            box.className = 'designer-row-ops';
            const up = U.el('button', 'icon-btn', '');
            up.type = 'button';
            up.setAttribute('data-icon', 'up');
            up.title = '上移';
            up.disabled = index === 0;
            up.addEventListener('click', function () {
                moveItem(list, index, -1);
                rerender();
                changed();
            });
            const down = U.el('button', 'icon-btn', '');
            down.type = 'button';
            down.setAttribute('data-icon', 'down');
            down.title = '下移';
            down.disabled = index === list.length - 1;
            down.addEventListener('click', function () {
                moveItem(list, index, 1);
                rerender();
                changed();
            });
            const remove = U.el('button', 'icon-btn', '');
            remove.type = 'button';
            remove.setAttribute('data-icon', 'trash');
            remove.title = '删除';
            remove.addEventListener('click', function () {
                list.splice(index, 1);
                rerender();
                changed();
            });
            box.appendChild(up);
            box.appendChild(down);
            box.appendChild(remove);
            if (window.Icons) window.Icons.mount(box);
            return box;
        }

        function moveItem(list, index, delta) {
            const target = index + delta;
            if (target < 0 || target >= list.length) return;
            const item = list[index];
            list[index] = list[target];
            list[target] = item;
        }

        function renderColumns() {
            const tbody = dom.designerColumns.querySelector('tbody');
            U.clear(tbody);
            const supports = current.supports || {};
            current.draft.columns.forEach(function (column, index) {
                const tr = document.createElement('tr');

                tr.appendChild(cell(textInput(column.name, function (value) {
                    column.name = value;
                    if (column._orig) {
                        column.oldName = value === column._orig ? '' : column._orig;
                    }
                    fillColumnList();
                    changed();
                }, { placeholder: '列名' })));

                tr.appendChild(cell(textInput(column.type, function (value) {
                    column.type = value;
                    changed();
                }, { list: 'designerTypeList', placeholder: '如 VARCHAR(64)' })));

                tr.appendChild(cell(checkbox(column.notNull, function (value) {
                    column.notNull = value;
                    changed();
                }), 'col-flag'));

                tr.appendChild(cell(checkbox(column.primaryKey, function (value) {
                    column.primaryKey = value;
                    changed();
                }), 'col-flag'));

                tr.appendChild(cell(checkbox(column.autoIncrement, function (value) {
                    column.autoIncrement = value;
                    changed();
                }), 'col-flag'));

                tr.appendChild(cell(textInput(column.default, function (value) {
                    column.default = value;
                    column.hasDefault = String(value).trim() !== '';
                    changed();
                }, { placeholder: "0 / 'x' / CURRENT_TIMESTAMP" })));

                tr.appendChild(cell(textInput(column.comment, function (value) {
                    column.comment = value;
                    changed();
                }, {
                    disabled: !supports.columnComment,
                    placeholder: supports.columnComment ? '注释' : 'SQLite 不支持'
                })));

                tr.appendChild(cell(rowOps(index, current.draft.columns, renderColumns), 'col-ops'));
                tbody.appendChild(tr);
            });
        }

        function renderIndexes() {
            const tbody = dom.designerIndexes.querySelector('tbody');
            U.clear(tbody);
            current.draft.indexes.forEach(function (index, position) {
                const tr = document.createElement('tr');
                tr.appendChild(cell(textInput(index.name, function (value) {
                    index.name = value;
                    changed();
                }, { placeholder: '留空自动命名' })));
                tr.appendChild(cell(checkbox(index.unique, function (value) {
                    index.unique = value;
                    changed();
                }), 'col-flag'));
                tr.appendChild(cell(textInput((index.columns || []).join(', '), function (value) {
                    index.columns = splitColumns(value);
                    changed();
                }, { list: 'designerColumnList', placeholder: 'col1, col2' })));
                tr.appendChild(cell(rowOps(position, current.draft.indexes, renderIndexes), 'col-ops'));
                tbody.appendChild(tr);
            });
        }

        function actionSelect(value, onChange) {
            const select = document.createElement('select');
            select.className = 'designer-input';
            ACTIONS.forEach(function (action) {
                const option = document.createElement('option');
                option.value = action;
                option.textContent = action;
                select.appendChild(option);
            });
            select.value = ACTIONS.indexOf(value) >= 0 ? value : 'NO ACTION';
            select.addEventListener('change', function () { onChange(select.value); });
            return select;
        }

        function renderForeignKeys() {
            const tbody = dom.designerForeignKeys.querySelector('tbody');
            U.clear(tbody);
            current.draft.foreignKeys.forEach(function (fk, position) {
                const tr = document.createElement('tr');
                tr.appendChild(cell(textInput(fk.name, function (value) {
                    fk.name = value;
                    changed();
                }, { placeholder: '留空自动命名' })));
                tr.appendChild(cell(textInput((fk.columns || []).join(', '), function (value) {
                    fk.columns = splitColumns(value);
                    changed();
                }, { list: 'designerColumnList', placeholder: '本表列' })));
                tr.appendChild(cell(textInput(fk.refTable, function (value) {
                    fk.refTable = value;
                    changed();
                }, { list: 'designerTableList', placeholder: '引用表' })));
                tr.appendChild(cell(textInput((fk.refColumns || []).join(', '), function (value) {
                    fk.refColumns = splitColumns(value);
                    changed();
                }, { placeholder: '引用列' })));
                tr.appendChild(cell(actionSelect(fk.onUpdate, function (value) {
                    fk.onUpdate = value;
                    changed();
                })));
                tr.appendChild(cell(actionSelect(fk.onDelete, function (value) {
                    fk.onDelete = value;
                    changed();
                })));
                tr.appendChild(cell(rowOps(position, current.draft.foreignKeys, renderForeignKeys),
                    'col-ops'));
                tbody.appendChild(tr);
            });
        }

        function splitColumns(text) {
            return String(text || '').split(',').map(function (part) {
                return part.trim();
            }).filter(Boolean);
        }

        function addColumn() {
            const engine = current.engine;
            current.draft.columns.push({
                name: '', type: engine === 'mysql' ? 'VARCHAR(255)' :
                    (engine === 'postgres' ? 'text' : 'TEXT'),
                notNull: false, primaryKey: false, autoIncrement: false,
                hasDefault: false, default: '', comment: '', oldName: '', _orig: ''
            });
            renderColumns();
            changed();
        }

        function addIndex() {
            const first = current.draft.columns.length ? current.draft.columns[0].name : '';
            current.draft.indexes.push({ name: '', unique: false, columns: first ? [first] : [] });
            renderIndexes();
            changed();
        }

        function addForeignKey() {
            current.draft.foreignKeys.push({
                name: '', columns: [], refTable: '', refColumns: [],
                onUpdate: 'NO ACTION', onDelete: 'NO ACTION'
            });
            renderForeignKeys();
            changed();
        }

        function changed() {
            if (!current) return;
            current.token = null;
            current.preview = null;
            dom.designerApply.disabled = true;
            dom.designerSql.textContent = '变更已修改，请重新预览 SQL。';
            renderWarnings([]);
            renderImpact('');
            updateDirty();
        }

        function dirty() {
            return !!(current && JSON.stringify(payload()) !== current.snapshot);
        }

        function updateDirty() {
            if (!current) return;
            const isDirty = dirty();
            dom.designerDirty.hidden = !isDirty;
            if (!current.token) dom.designerApply.disabled = true;
            else dom.designerApply.disabled = isDirty;
            if (isDirty) dom.designerFootNote.textContent = '存在未应用的变更';
            else if (current.token) dom.designerFootNote.textContent = 'SQL 已确认，可执行';
            else dom.designerFootNote.textContent = '执行前需先预览 SQL';
        }

        function renderWarnings(warnings) {
            U.clear(dom.designerWarnings);
            (warnings || []).forEach(function (warning) {
                dom.designerWarnings.appendChild(U.el('div', 'designer-warning', warning));
            });
        }

        function renderImpact(affectedRows) {
            if (!dom.designerImpact) return;
            const text = affectedRows === undefined || affectedRows === null || affectedRows === ''
                ? '' : String(affectedRows);
            dom.designerImpact.textContent = text ? ('影响行数：' + text) : '';
            dom.designerImpact.hidden = !text;
        }

        function previewArgs() {
            return {
                sessionId: current.session.id,
                mode: current.mode,
                ref: tableRef(),
                definition: payload()
            };
        }

        async function preview() {
            if (!current || busy) return;
            clearError();
            busy = true;
            dom.designerPreview.disabled = true;
            dom.designerPreview2.disabled = true;
            dom.designerApply.disabled = true;
            dom.designerFootNote.textContent = '正在生成 SQL…';
            try {
                const result = await api.ddlPreview(previewArgs());
                current.preview = result;
                current.token = result.token || null;
                current.snapshot = JSON.stringify(payload());
                dom.designerSql.textContent = result.sql || '（没有需要执行的语句）';
                renderWarnings(result.warnings || []);
                renderImpact(result.affectedRows);
                showPanel('sql');
                if (!result.statements || !result.statements.length) current.token = null;
                updateDirty();
            } catch (error) {
                showError('生成 SQL 失败：' + errorText(error));
                dom.designerFootNote.textContent = '生成失败，可修改后重试';
            } finally {
                busy = false;
                dom.designerPreview.disabled = false;
                dom.designerPreview2.disabled = false;
                updateDirty();
            }
        }

        function formatApplyError(error) {
            const details = (error && error.details) || {};
            let text = '执行失败：' + errorText(error);
            if (typeof details.statementIndex === 'number') {
                text += '\n失败语句 #' + (details.statementIndex + 1) + '：';
                if (details.statement) text += '\n' + details.statement;
            }
            if (typeof details.applied === 'number' && details.applied > 0) {
                text += '\n已成功执行 ' + details.applied + ' 条语句。';
            }
            if (details.rolledBack) text += '\n事务已回滚，数据库未发生变更，可修改后重试。';
            else if (typeof details.applied === 'number' && details.applied > 0) {
                text += '\nMySQL DDL 不可回滚，请检查数据库当前状态。';
            }
            return text;
        }

        async function apply() {
            if (!current || busy || !current.token) return;
            if (dirty()) {
                changed();
                toast('变更已修改，请重新预览', 'warn');
                return;
            }
            clearError();
            busy = true;
            dom.designerApply.disabled = true;
            dom.designerApply.textContent = '执行中…';
            const args = previewArgs();
            args.token = current.token;
            try {
                const result = await api.ddlApply(args);
                const sessionId = current.session.id;
                const names = {
                    mode: current.mode,
                    originalName: current.originalName,
                    newName: current.draft.name,
                    dropped: false
                };
                toast('DDL 已执行 · ' + (result.applied || 0) + ' 条语句');
                close();
                if (ctx.afterDdl) ctx.afterDdl(sessionId, names);
                else if (ctx.tree) ctx.tree.refreshSession(sessionId);
            } catch (error) {
                showError(formatApplyError(error));
                toast('DDL 执行失败：' + errorText(error), 'error');
            } finally {
                busy = false;
                dom.designerApply.textContent = '执行';
                updateDirty();
            }
        }

        async function requestClose() {
            if (!current) return;
            if (dirty() && !(await U.confirm({
                title: '关闭设计器',
                message: '有未应用的变更，确定关闭设计器？',
                hint: '未应用的列 / 索引 / 外键变更会丢失。',
                okText: '关闭',
                danger: true
            }))) return;
            close();
        }

        function close() {
            dom.designerDialog.hidden = true;
            current = null;
            dom.designerError.hidden = true;
            dom.designerApply.textContent = '执行';
        }

        // ------------------------------------------------------------ 删除/清空/库操作
        function openConfirm() {
            dom.ddlConfirm.hidden = false;
            dom.ddlConfirmError.hidden = true;
            dom.ddlConfirmError.textContent = '';
            dom.ddlConfirmRun.disabled = true;
            dom.ddlConfirmRun.textContent = '执行';
            dom.ddlConfirmClose.focus();
        }

        function closeConfirm() {
            dom.ddlConfirm.hidden = true;
            confirmState = null;
        }

        function updateConfirmRunState() {
            if (!confirmState) return;
            if (!confirmState.token) {
                dom.ddlConfirmRun.disabled = true;
                return;
            }
            const required = confirmState.config.requireName;
            if (!required) {
                dom.ddlConfirmRun.disabled = false;
                return;
            }
            dom.ddlConfirmRun.disabled = dom.ddlConfirmName.value.trim() !== required;
        }

        async function previewDestructive(config) {
            confirmState = { config: config, token: null };
            dom.ddlConfirmTitle.textContent = config.title || '确认执行 DDL';
            dom.ddlConfirmSub.textContent = config.sub || '';
            dom.ddlConfirmNote.textContent = config.note || '';
            dom.ddlConfirmSql.textContent = '正在生成 SQL…';
            U.clear(dom.ddlConfirmWarn);
            dom.ddlConfirmNameField.hidden = !config.requireName;
            dom.ddlConfirmName.value = '';
            dom.ddlConfirmName.placeholder = config.requireName || '';
            openConfirm();

            try {
                const result = await api.ddlPreview({
                    sessionId: config.session.id,
                    mode: config.mode,
                    ref: config.ref || {},
                    definition: config.definition || {},
                    newName: config.newName || ''
                });
                if (!confirmState || confirmState.config !== config) return;
                confirmState.token = result.token || null;
                dom.ddlConfirmSql.textContent = result.sql || '（没有需要执行的语句）';
                if (result.affectedRows !== undefined && result.affectedRows !== null &&
                    result.affectedRows !== '') {
                    dom.ddlConfirmWarn.appendChild(
                        U.el('div', 'designer-impact', '影响行数：' + result.affectedRows));
                }
                (result.warnings || []).forEach(function (warning) {
                    dom.ddlConfirmWarn.appendChild(U.el('div', 'designer-warning', warning));
                });
                dom.ddlConfirmRun.disabled = config.requireName || !confirmState.token;
                updateConfirmRunState();
            } catch (error) {
                dom.ddlConfirmSql.textContent = '';
                dom.ddlConfirmError.textContent = '生成 SQL 失败：' + errorText(error);
                dom.ddlConfirmError.hidden = false;
            }
        }

        async function runConfirm() {
            if (!confirmState || !confirmState.token) return;
            const config = confirmState.config;
            dom.ddlConfirmRun.disabled = true;
            dom.ddlConfirmRun.textContent = '执行中…';
            dom.ddlConfirmError.hidden = true;
            try {
                const result = await api.ddlApply({
                    sessionId: config.session.id,
                    mode: config.mode,
                    ref: config.ref || {},
                    definition: config.definition || {},
                    newName: config.newName || '',
                    token: confirmState.token,
                    confirmName: dom.ddlConfirmName.value.trim()
                });
                const sessionId = config.session.id;
                const dropped = config.mode === 'drop' || config.mode === 'database.drop';
                const name = config.requireName || config.newName || '';
                toast('DDL 已执行 · ' + (result.applied || 0) + ' 条语句');
                closeConfirm();
                if (ctx.afterDdl) {
                    ctx.afterDdl(sessionId, {
                        mode: config.mode,
                        originalName: name,
                        newName: name,
                        dropped: dropped
                    });
                } else if (ctx.tree) {
                    ctx.tree.refreshSession(sessionId);
                }
            } catch (error) {
                dom.ddlConfirmError.textContent = formatApplyError(error);
                dom.ddlConfirmError.hidden = false;
                dom.ddlConfirmRun.textContent = '执行';
                updateConfirmRunState();
            }
        }

        function openDropTable(session, ref) {
            if (!session || !ref || !ref.name) return;
            const scope = ref.database || ref.schema || '';
            previewDestructive({
                session: session,
                title: '删除表',
                sub: ref.name + ' · ' + scope,
                mode: 'drop',
                ref: ref,
                definition: { name: ref.name, scope: scope },
                requireName: ref.name,
                note: '删除表会同时删除表内数据，且不可恢复。请输入表名确认。'
            });
        }

        function openTruncateTable(session, ref) {
            if (!session || !ref || !ref.name) return;
            const scope = ref.database || ref.schema || '';
            previewDestructive({
                session: session,
                title: '清空表',
                sub: ref.name + ' · ' + scope,
                mode: 'truncate',
                ref: ref,
                definition: { name: ref.name, scope: scope },
                requireName: ref.name,
                note: '清空表会删除全部行，且不可撤销。请输入表名确认。'
            });
        }

        function openCreateDatabase(session, ref) {
            if (!session) return;
            const name = window.prompt('新建数据库名称：');
            if (name == null) return;
            const trimmed = name.trim();
            if (!trimmed) {
                toast('数据库名不能为空', 'warn');
                return;
            }
            previewDestructive({
                session: session,
                title: '新建数据库',
                sub: trimmed,
                mode: 'database.create',
                ref: { database: trimmed },
                definition: {
                    name: trimmed,
                    options: { engine: '', charset: 'utf8mb4', collation: '', comment: '' }
                },
                newName: trimmed,
                requireName: '',
                note: 'MySQL 默认使用 utf8mb4；PostgreSQL 使用数据库默认编码。'
            });
        }

        function openDropDatabase(session, ref) {
            if (!session) return;
            const name = (ref && (ref.database || ref.name)) || '';
            if (!name) return;
            previewDestructive({
                session: session,
                title: '删除数据库',
                sub: name,
                mode: 'database.drop',
                ref: { database: name },
                definition: { name: name, scope: name },
                newName: name,
                requireName: name,
                note: '删除数据库不可恢复，且会断开其他会话。请输入数据库名确认。'
            });
        }

        return {
            init: init,
            openTableDesigner: openTableDesigner,
            openDropTable: openDropTable,
            openTruncateTable: openTruncateTable,
            openCreateDatabase: openCreateDatabase,
            openDropDatabase: openDropDatabase
        };
    }

    window.SilkitDesigner = { create: create };
})();
