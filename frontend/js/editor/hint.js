// 自动补全：按会话缓存 表/列 元数据，供 CodeMirror sql-hint 使用（M4-4）。
// 元数据来自 meta.children（folder:tables / folder:columns），失败时静默降级为关键字提示。
(function () {
    'use strict';

    const U = window.SilkitUtil;

    const KEYWORDS = {
        sqlite: ['SELECT', 'FROM', 'WHERE', 'AND', 'OR', 'NOT', 'NULL', 'IS', 'IN',
            'LIKE', 'BETWEEN', 'EXISTS', 'ORDER BY', 'GROUP BY', 'HAVING', 'LIMIT',
            'OFFSET', 'JOIN', 'LEFT JOIN', 'INNER JOIN', 'CROSS JOIN', 'ON', 'AS',
            'DISTINCT', 'INSERT INTO', 'VALUES', 'UPDATE', 'SET', 'DELETE FROM',
            'CREATE TABLE', 'DROP TABLE', 'ALTER TABLE', 'PRIMARY KEY',
            'FOREIGN KEY', 'REFERENCES', 'UNIQUE', 'INDEX', 'VIEW', 'BEGIN',
            'COMMIT', 'ROLLBACK', 'PRAGMA', 'EXPLAIN QUERY PLAN', 'CASE WHEN THEN ELSE END'],
        mysql: ['SELECT', 'FROM', 'WHERE', 'AND', 'OR', 'NOT', 'NULL', 'IS', 'IN',
            'LIKE', 'BETWEEN', 'EXISTS', 'ORDER BY', 'GROUP BY', 'HAVING', 'LIMIT',
            'OFFSET', 'JOIN', 'LEFT JOIN', 'RIGHT JOIN', 'INNER JOIN', 'OUTER JOIN',
            'ON', 'AS', 'DISTINCT', 'INSERT INTO', 'VALUES', 'UPDATE', 'SET',
            'DELETE FROM', 'CREATE TABLE', 'DROP TABLE', 'ALTER TABLE',
            'SHOW TABLES', 'SHOW DATABASES', 'SHOW CREATE TABLE', 'DESCRIBE',
            'PRIMARY KEY', 'AUTO_INCREMENT', 'ENGINE', 'DEFAULT CHARSET',
            'USE', 'EXPLAIN', 'EXPLAIN ANALYZE', 'BEGIN', 'COMMIT', 'ROLLBACK',
            'CASE WHEN THEN ELSE END', 'NOW()', 'DATE_FORMAT()', 'COALESCE()'],
        postgres: ['SELECT', 'FROM', 'WHERE', 'AND', 'OR', 'NOT', 'NULL', 'IS',
            'IN', 'LIKE', 'ILIKE', 'BETWEEN', 'EXISTS', 'ORDER BY', 'GROUP BY',
            'HAVING', 'LIMIT', 'OFFSET', 'JOIN', 'LEFT JOIN', 'RIGHT JOIN',
            'INNER JOIN', 'OUTER JOIN', 'ON', 'AS', 'DISTINCT', 'INSERT INTO',
            'VALUES', 'UPDATE', 'SET', 'DELETE FROM', 'CREATE TABLE', 'DROP TABLE',
            'ALTER TABLE', 'PRIMARY KEY', 'REFERENCES', 'UNIQUE', 'SCHEMA',
            'RETURNING', 'EXPLAIN', 'EXPLAIN ANALYZE', 'BEGIN', 'COMMIT',
            'ROLLBACK', 'CASE WHEN THEN ELSE END', 'COALESCE()', 'NULLIF()']
    };

    function create(ctx) {
        const api = ctx.api;
        const state = ctx.state;
        const toast = ctx.toast;

        // sessionId -> { tables: {name: [cols]}, keywords: [...], loadedAt }
        const cache = new Map();
        const pending = new Map();

        function tableRef(session, folderName) {
            const profile = session.profile || {};
            const engine = profile.engine || 'sqlite';
            const ref = { kind: 'folder', name: folderName || 'tables' };
            if (engine === 'mysql') {
                ref.database = session.currentDb || profile.database || '';
            } else if (engine === 'postgres') {
                ref.database = session.currentDb || profile.database || '';
                ref.schema = profile.schema || 'public';
            } else {
                ref.database = 'main';
            }
            return ref;
        }

        async function tablesOf(session) {
            const data = await api.metaChildren(session.id, tableRef(session, 'tables'));
            return (data.nodes || []).filter(function (node) {
                return node.kind === 'table' || node.kind === 'view';
            });
        }

        async function columnsOf(session, name) {
            const ref = tableRef(session, 'columns');
            ref.table = name;
            try {
                const data = await api.metaChildren(session.id, ref);
                return (data.nodes || []).map(function (node) { return node.name; });
            } catch (error) {
                return [];
            }
        }

        async function load(session) {
            const sid = session.id;
            if (cache.has(sid)) return cache.get(sid);
            if (pending.has(sid)) return pending.get(sid);
            if (!session.profile) {
                return { tables: {}, keywords: KEYWORDS.sqlite };
            }
            const profile = session.profile || {};
            const engine = profile.engine || 'sqlite';

            const task = (async function () {
                const entry = { tables: {}, keywords: KEYWORDS[engine] || KEYWORDS.sqlite, loadedAt: Date.now() };
                try {
                    const tables = await tablesOf(session);
                    // 最多预取 60 张表的列，避免大库一次拉全量。
                    const targets = tables.slice(0, 60);
                    for (const table of targets) {
                        const columns = await columnsOf(session, table.name);
                        entry.tables[table.name] = columns.length ? columns : [];
                    }
                } catch (error) {
                    console.warn('[silkit] 补全元数据加载失败', error);
                }
                cache.set(sid, entry);
                pending.delete(sid);
                return entry;
            })();
            pending.set(sid, task);
            return task;
        }

        function invalidate(sessionId) {
            cache.delete(sessionId);
            pending.delete(sessionId);
        }

        // ---------------------------------------------------------- hint 函数
        function makeHint(session) {
            return function (editor, options) {
                const opts = options || {};
                const entry = cache.get(session.id) || { tables: {} };
                opts.tables = entry.tables;
                const list = window.CodeMirror.hint.sql(editor, opts);
                if (session.profile) {
                    const engine = session.profile.engine || 'sqlite';
                    list.list = list.list.concat(KEYWORDS[engine] || KEYWORDS.sqlite)
                        .filter(function (word, index, all) { return all.indexOf(word) === index; });
                }
                return list;
            };
        }

        return {
            ensure: function (session) { return load(session); },
            hint: makeHint,
            invalidate: invalidate,
            prefetch: function (session) {
                if (!session || !session.profile) return;
                load(session).catch(function (error) {
                    console.warn('[silkit] 补全预取失败', error);
                });
            }
        };
    }

    window.SilkitHint = { create: create };
})();
