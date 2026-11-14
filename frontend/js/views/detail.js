// 对象详情/DDL 弹窗：列、索引、外键、触发器与建表 SQL。
(function () {
    'use strict';

    const U = window.SilkitUtil;

    function create(ctx) {
        const api = ctx.api;
        const toast = ctx.toast;

        const dom = {};
        let lastSql = '';
        let lastSqlName = 'ddl';

        function byId(id) { return document.getElementById(id); }

        function init() {
            ['detailDialog', 'detailTitle', 'detailSub', 'detailBody', 'detailClose', 'detailCopy',
                'detailSave', 'detailKind']
                .forEach(function (id) { dom[id] = byId(id); });
            if (!dom.detailDialog) return;

            dom.detailClose.addEventListener('click', close);
            dom.detailDialog.addEventListener('click', function (event) {
                if (event.target === dom.detailDialog) close();
            });
            document.addEventListener('keydown', function (event) {
                if (event.key === 'Escape' && !dom.detailDialog.hidden) close();
            });
            dom.detailCopy.addEventListener('click', function () {
                if (!lastSql) {
                    toast('没有可复制的 SQL', 'error');
                    return;
                }
                U.copyText(lastSql).then(function () {
                    toast('DDL 已复制');
                }).catch(function (error) {
                    toast('复制失败：' + (error.message || error), 'error');
                });
            });
            if (dom.detailSave) {
                dom.detailSave.addEventListener('click', function () {
                    if (!lastSql) {
                        toast('没有可保存的 SQL', 'error');
                        return;
                    }
                    api.pickFile({
                        kind: 'sql',
                        save: true,
                        defaultName: lastSqlName + '.sql'
                    }).then(function (picked) {
                        if (!picked || !picked.path) return;
                        return api.appSaveFile(picked.path, lastSql).then(function () {
                            toast('DDL 已保存到 ' + picked.path);
                        });
                    }).catch(function (error) {
                        toast('保存失败：' + (error.message || error), 'error');
                    });
                });
            }
        }

        function open() {
            dom.detailDialog.hidden = false;
            dom.detailClose.focus();
        }

        function close() {
            if (dom.detailDialog) dom.detailDialog.hidden = true;
        }

        function loading(title, sub) {
            dom.detailTitle.textContent = title;
            dom.detailSub.textContent = sub || '';
            U.clear(dom.detailBody);
            const box = U.el('div', 'detail-loading', '正在读取元数据…');
            dom.detailBody.appendChild(box);
            if (dom.detailCopy) dom.detailCopy.hidden = true;
            if (dom.detailSave) dom.detailSave.hidden = true;
            lastSql = '';
            open();
        }

        function section(title, count) {
            const box = document.createElement('section');
            box.className = 'detail-section';
            const head = U.el('div', 'detail-section-head');
            head.appendChild(U.el('span', 'detail-section-title', title));
            if (count != null) head.appendChild(U.el('span', 'node-badge', String(count)));
            box.appendChild(head);
            dom.detailBody.appendChild(box);
            return box;
        }

        function empty(text) {
            dom.detailBody.appendChild(U.el('div', 'detail-empty', text));
        }

        function sqlBlock(sql) {
            const box = document.createElement('section');
            box.className = 'detail-section';
            const head = U.el('div', 'detail-section-head');
            head.appendChild(U.el('span', 'detail-section-title', 'DDL'));
            box.appendChild(head);
            box.appendChild(U.el('pre', 'detail-sql', sql || '（没有 SQL 定义）'));
            dom.detailBody.appendChild(box);
            lastSql = sql || '';
        }

        function table(headers, rows) {
            const wrap = U.el('div', 'detail-table-wrap');
            const tableEl = document.createElement('table');
            tableEl.className = 'detail-table';
            const thead = document.createElement('thead');
            const headRow = document.createElement('tr');
            headers.forEach(function (header) {
                headRow.appendChild(U.el('th', '', header));
            });
            thead.appendChild(headRow);
            tableEl.appendChild(thead);
            const tbody = document.createElement('tbody');
            rows.forEach(function (row) {
                const tr = document.createElement('tr');
                row.forEach(function (value) {
                    const td = U.el('td', '', value == null ? '' : String(value));
                    if (value == null) {
                        td.classList.add('cell-null');
                        td.textContent = 'NULL';
                    }
                    tr.appendChild(td);
                });
                tbody.appendChild(tr);
            });
            tableEl.appendChild(tbody);
            wrap.appendChild(tableEl);
            return wrap;
        }

        async function showDetail(sessionId, ref) {
            const kindLabel = ref.kind === 'view' ? '视图' : '表';
            loading(kindLabel + ' · ' + ref.name, (ref.database || 'main') + ' · ' + ref.kind);
            try {
                const detail = await api.metaDetail(sessionId, ref);
                dom.detailTitle.textContent = kindLabel + ' · ' + (detail.name || ref.name);
                const columns = Array.isArray(detail.columns) ? detail.columns : [];
                const indexes = Array.isArray(detail.indexes) ? detail.indexes : [];
                const foreignKeys = Array.isArray(detail.foreignKeys) ? detail.foreignKeys : [];
                const triggers = Array.isArray(detail.triggers) ? detail.triggers : [];
                dom.detailSub.textContent = (detail.database || ref.database || 'main') + ' · ' +
                    columns.length + ' 列 · ' + indexes.length + ' 索引 · ' +
                    foreignKeys.length + ' 外键 · ' + triggers.length + ' 触发器';
                U.clear(dom.detailBody);

                const columnBox = section('列', columns.length);
                if (!columns.length) {
                    columnBox.appendChild(U.el('div', 'detail-empty', '没有列信息'));
                } else {
                    columnBox.appendChild(table(
                        ['列名', '类型', '类型归类', '非空', '主键', '默认值'],
                        columns.map(function (column) {
                            return [
                                column.name,
                                column.type || '',
                                column.kind || '',
                                column.notNull ? 'NOT NULL' : '',
                                column.primaryKey ? 'PK' : '',
                                column.default
                            ];
                        })
                    ));
                }

                const indexBox = section('索引', indexes.length);
                if (!indexes.length) {
                    indexBox.appendChild(U.el('div', 'detail-empty', '没有索引'));
                } else {
                    indexBox.appendChild(table(
                        ['名称', '唯一', '来源', '部分索引', '列'],
                        indexes.map(function (index) {
                            return [
                                index.name,
                                index.unique ? '唯一' : '',
                                index.origin || '',
                                index.partial ? '是' : '',
                                (index.columns || []).join(', ')
                            ];
                        })
                    ));
                }

                const fkBox = section('外键', foreignKeys.length);
                if (!foreignKeys.length) {
                    fkBox.appendChild(U.el('div', 'detail-empty', '没有外键'));
                } else {
                    fkBox.appendChild(table(
                        ['本表列', '引用表', '引用列', 'ON UPDATE', 'ON DELETE'],
                        foreignKeys.map(function (fk) {
                            return [fk.from, fk.table, fk.to, fk.onUpdate, fk.onDelete];
                        })
                    ));
                }

                if (triggers.length) {
                    const triggerBox = section('触发器', triggers.length);
                    triggers.forEach(function (trigger) {
                        const item = document.createElement('details');
                        item.className = 'detail-trigger';
                        const summary = document.createElement('summary');
                        summary.textContent = trigger.name;
                        item.appendChild(summary);
                        item.appendChild(U.el('pre', 'detail-sql', trigger.sql || ''));
                        triggerBox.appendChild(item);
                    });
                }

                sqlBlock(detail.sql || '');
                lastSqlName = (detail.name || ref.name || 'ddl') + '-table';
                if (dom.detailCopy) dom.detailCopy.hidden = !lastSql;
                if (dom.detailSave) dom.detailSave.hidden = !lastSql;
                void dom.detailKind;
            } catch (error) {
                dom.detailBody.textContent = '读取失败：' + (error.message || error);
                toast('读取结构失败：' + (error.message || error), 'error');
            }
        }

        async function showDdl(sessionId, ref) {
            loading('DDL · ' + ref.name, (ref.database || 'main') + ' · ' + ref.kind);
            try {
                const result = await api.metaDdl(sessionId, ref);
                dom.detailTitle.textContent = 'DDL · ' + ref.name;
                dom.detailSub.textContent = (ref.database || 'main') + ' · ' + ref.kind +
                    (result.warnings && result.warnings.length ? ' · ' + result.warnings.length + ' 条提示' : '');
                U.clear(dom.detailBody);
                sqlBlock(result.sql || '');
                lastSqlName = (ref.name || 'ddl') + '-' + (ref.kind || 'ddl');
                if (result.warnings && result.warnings.length) {
                    const box = section('提示', result.warnings.length);
                    result.warnings.forEach(function (warning) {
                        box.appendChild(U.el('div', 'detail-warning', warning));
                    });
                }
                if (dom.detailCopy) dom.detailCopy.hidden = !lastSql;
                if (dom.detailSave) dom.detailSave.hidden = !lastSql;
            } catch (error) {
                dom.detailBody.textContent = '读取失败：' + (error.message || error);
                toast('读取 DDL 失败：' + (error.message || error), 'error');
            }
        }

        return { init: init, showDetail: showDetail, showDdl: showDdl, close: close };
    }

    window.SilkitDetail = { create: create };
})();
