// 智能补全（v1.0.3）：关键字 + 当前连接/库的表与视图 + 列。
//
//   * 缓存维度是 sessionId|engine|database|schema，切换库后不会串到上一个库的表。
//   * 输入 `表名.` / `别名.` 时只补全该表的列，并把列插在点号之后；解析不到表就返回空，
//     绝不回退成关键字 / 表名 / 其他表的列（避免「串列」）。
//   * FROM / JOIN / UPDATE / INTO 后的 `库名.`（或 PG `schema.`）是库限定符，提示该库的表名。
//   * 表引用按「光标所在的整条语句」解析（以分号切分），因此 SELECT 列表写在 FROM 之前时，
//     也能拿到光标之后 FROM 的表；裸列名只来自语句里的这些表，不再混入树里选中的其它表。
//   * 每个候选右侧统一显示所属路径：列 = `库.表`（PG 为 `schema.表`），表 = 库/schema 名，
//     关键字 = keyword；show-hint 的自定义 render 负责右对齐，名称不再跟 `(table)` / `(view)`。
//   * 返回 Promise（CodeMirror show-hint 支持异步 hint），首次触发时现取元数据。
(function () {
    'use strict';

    const KEYWORDS = {
        sqlite: ['SELECT', 'FROM', 'WHERE', 'AND', 'OR', 'NOT', 'NULL', 'IS', 'IN',
            'LIKE', 'BETWEEN', 'EXISTS', 'ORDER BY', 'GROUP BY', 'HAVING', 'LIMIT',
            'OFFSET', 'JOIN', 'LEFT JOIN', 'INNER JOIN', 'CROSS JOIN', 'ON', 'AS',
            'DISTINCT', 'INSERT INTO', 'VALUES', 'UPDATE', 'SET', 'DELETE FROM',
            'CREATE TABLE', 'DROP TABLE', 'ALTER TABLE', 'PRIMARY KEY',
            'FOREIGN KEY', 'REFERENCES', 'UNIQUE', 'INDEX', 'VIEW', 'BEGIN',
            'COMMIT', 'ROLLBACK', 'PRAGMA', 'EXPLAIN QUERY PLAN', 'CASE WHEN THEN ELSE END',
            'WITH', 'UNION ALL', 'COUNT(*)', 'COALESCE()'],
        mysql: ['SELECT', 'FROM', 'WHERE', 'AND', 'OR', 'NOT', 'NULL', 'IS', 'IN',
            'LIKE', 'BETWEEN', 'EXISTS', 'ORDER BY', 'GROUP BY', 'HAVING', 'LIMIT',
            'OFFSET', 'JOIN', 'LEFT JOIN', 'RIGHT JOIN', 'INNER JOIN', 'OUTER JOIN',
            'ON', 'AS', 'DISTINCT', 'INSERT INTO', 'VALUES', 'UPDATE', 'SET',
            'DELETE FROM', 'CREATE TABLE', 'DROP TABLE', 'ALTER TABLE',
            'SHOW TABLES', 'SHOW DATABASES', 'SHOW CREATE TABLE', 'DESCRIBE',
            'PRIMARY KEY', 'AUTO_INCREMENT', 'ENGINE', 'DEFAULT CHARSET',
            'USE', 'EXPLAIN', 'EXPLAIN ANALYZE', 'BEGIN', 'COMMIT', 'ROLLBACK',
            'CASE WHEN THEN ELSE END', 'WITH', 'UNION ALL', 'NOW()',
            'DATE_FORMAT()', 'COALESCE()', 'COUNT(*)'],
        postgres: ['SELECT', 'FROM', 'WHERE', 'AND', 'OR', 'NOT', 'NULL', 'IS',
            'IN', 'LIKE', 'ILIKE', 'BETWEEN', 'EXISTS', 'ORDER BY', 'GROUP BY',
            'HAVING', 'LIMIT', 'OFFSET', 'JOIN', 'LEFT JOIN', 'RIGHT JOIN',
            'INNER JOIN', 'OUTER JOIN', 'ON', 'AS', 'DISTINCT', 'INSERT INTO',
            'VALUES', 'UPDATE', 'SET', 'DELETE FROM', 'CREATE TABLE', 'DROP TABLE',
            'ALTER TABLE', 'PRIMARY KEY', 'REFERENCES', 'UNIQUE', 'SCHEMA',
            'RETURNING', 'EXPLAIN', 'EXPLAIN ANALYZE', 'BEGIN', 'COMMIT',
            'ROLLBACK', 'CASE WHEN THEN ELSE END', 'COALESCE()', 'NULLIF()',
            'WITH', 'UNION ALL', 'COUNT(*)', 'SET search_path TO']
    };

    // 出现在表/别名候选位置的词，不能被误判成别名。
    const STOP_WORDS = new Set(['WHERE', 'JOIN', 'LEFT', 'RIGHT', 'INNER', 'OUTER',
        'CROSS', 'FULL', 'ON', 'GROUP', 'ORDER', 'HAVING', 'LIMIT', 'OFFSET',
        'SET', 'VALUES', 'AND', 'OR', 'UNION', 'SELECT', 'UPDATE', 'DELETE',
        'INSERT', 'AS', 'USING', 'WITH', 'WHEN', 'CASE', 'INDEX', 'TABLE',
        'DISTINCT', 'RETURNING', 'FOR', 'INTO']);

    // 候选顺序：常用 SQL 关键字 → 表名 → 列名 → 冷门关键字（函数/DDL 子句/引擎专属项）。
    // 冷门项仍然保留，只是排到末尾，避免占住首屏。
    const COMMON_KEYWORDS = new Set([
        'SELECT', 'FROM', 'WHERE', 'AND', 'OR', 'NOT', 'NULL', 'IS', 'IN',
        'LIKE', 'ILIKE', 'BETWEEN', 'EXISTS',
        'ORDER BY', 'GROUP BY', 'HAVING', 'LIMIT', 'OFFSET',
        'JOIN', 'LEFT JOIN', 'INNER JOIN', 'ON', 'AS', 'DISTINCT',
        'INSERT INTO', 'VALUES', 'UPDATE', 'SET', 'DELETE FROM',
        'CREATE TABLE', 'DROP TABLE', 'ALTER TABLE',
        'EXPLAIN', 'BEGIN', 'COMMIT', 'ROLLBACK', 'WITH', 'UNION ALL'
    ]);
    const PRIORITY = { keyword: 0, table: 1, column: 2, rareKeyword: 3 };

    function unquote(name) {
        return String(name || '').replace(/^[`"\[]/, '').replace(/[`"\]]$/, '');
    }

    // 候选右侧显示的所属作用域：MySQL/SQLite 为库名，PG 为 schema 名。
    function scopeLabel(scope) {
        if (scope.engine === 'postgres') return scope.schema || scope.database || 'public';
        return scope.database || 'main';
    }

    // 光标所在的单条语句（以分号为界）。SELECT 列表通常写在 FROM 之前，
    // 只看光标之前的文本会漏掉尚未输入 / 位于光标之后的 FROM 子句。
    function statementAt(before, fullText) {
        const text = String(fullText || '');
        if (!text) return String(before || '');
        const offset = Math.min(String(before || '').length, text.length);
        const start = text.lastIndexOf(';', Math.max(0, offset - 1)) + 1;
        let end = text.indexOf(';', offset);
        if (end < 0) end = text.length;
        return text.slice(start, end);
    }

    // show-hint 支持 hint.render(elt, data, cur)：左侧候选名，右侧统一放类型与所属路径。
    function renderHintItem(elt, data, cur) {
        const doc = elt.ownerDocument || document;
        const label = doc.createElement('span');
        label.className = 'cm-hint-label';
        label.appendChild(doc.createTextNode(cur.text));
        elt.appendChild(label);

        const meta = doc.createElement('span');
        meta.className = 'cm-hint-meta';
        if (cur._badge) {
            const badge = doc.createElement('span');
            badge.className = 'cm-hint-badge';
            badge.appendChild(doc.createTextNode(cur._badge));
            meta.appendChild(badge);
        }
        if (cur._source) {
            const source = doc.createElement('span');
            source.className = 'cm-hint-source';
            source.appendChild(doc.createTextNode(cur._source));
            meta.appendChild(source);
        }
        if (meta.childNodes.length) elt.appendChild(meta);
    }

    function create(ctx) {
        const api = ctx.api;

        const cache = new Map();        // key -> { tables: Map, keywords }
        const pending = new Map();      // key -> Promise
        const columnTasks = new Map();  // key|table -> Promise
        const scopeCache = new Map();   // key -> 可用库 / schema 名
        const scopeTasks = new Map();   // key -> Promise

        function engineOf(session) {
            return (session && session.profile && session.profile.engine) ||
                (session && session.engine) || 'sqlite';
        }

        function scopeOf(session, getContext) {
            const context = (typeof getContext === 'function' ? getContext() : getContext) || {};
            const engine = engineOf(session);
            const profile = (session && session.profile) || {};
            return {
                engine: engine,
                database: context.database || (session && session.currentDb) ||
                    profile.database || (engine === 'sqlite' ? 'main' : ''),
                schema: context.schema || (engine === 'postgres'
                    ? (profile.schema || 'public') : ''),
                table: context.table || ''
            };
        }

        function keyOf(session, scope) {
            return [session.id, scope.engine, scope.database, scope.schema].join('|');
        }

        function folderRef(scope, folder, table) {
            const ref = { kind: 'folder', name: folder, database: scope.database || 'main' };
            if (scope.schema) ref.schema = scope.schema;
            if (table) ref.table = table;
            return ref;
        }

        async function loadTables(session, scope) {
            const key = keyOf(session, scope);
            if (cache.has(key)) return cache.get(key);
            if (pending.has(key)) return pending.get(key);
            const task = (async function () {
                const entry = {
                    tables: new Map(),
                    keywords: KEYWORDS[scope.engine] || KEYWORDS.sqlite,
                    at: Date.now()
                };
                await Promise.all(['tables', 'views'].map(async function (folder) {
                    try {
                        const data = await api.metaChildren(session.id, folderRef(scope, folder));
                        const nodes = data && Array.isArray(data.nodes) ? data.nodes : [];
                        nodes.forEach(function (node) {
                            if (!node || !node.name) return;
                            const kind = node.kind === 'view' ? 'view' : 'table';
                            entry.tables.set(node.name, { name: node.name, kind: kind, columns: null });
                        });
                    } catch (error) {
                        console.warn('[silkit] 补全读取 ' + folder + ' 失败', error);
                    }
                }));
                cache.set(key, entry);
                pending.delete(key);
                return entry;
            })();
            pending.set(key, task);
            return task;
        }

        // 库 / schema 名：用于识别 `db.` 这类限定符（FROM 后应提示表名，而不是列）。
        async function loadScopes(session, scope) {
            const key = keyOf(session, scope);
            if (scopeCache.has(key)) return scopeCache.get(key);
            if (scopeTasks.has(key)) return scopeTasks.get(key);
            const task = (async function () {
                const names = [];
                if (scope.engine === 'sqlite') {
                    names.push('main');
                } else {
                    try {
                        const parent = scope.engine === 'postgres'
                            ? { kind: 'folder', name: 'databases', database: scope.database }
                            : { kind: 'connection' };
                        const data = await api.metaChildren(session.id, parent);
                        const nodes = data && Array.isArray(data.nodes) ? data.nodes : [];
                        nodes.forEach(function (node) {
                            if (node && node.name && (node.kind === 'database' || node.kind === 'folder')) {
                                names.push(node.name);
                            }
                        });
                    } catch (error) {
                        // 库列表不可用时退化为「仅当前库」，不影响普通补全。
                    }
                }
                if (scope.database) names.push(scope.database);
                if (scope.schema) names.push(scope.schema);
                const list = Array.from(new Set(names));
                scopeCache.set(key, list);
                scopeTasks.delete(key);
                return list;
            })();
            scopeTasks.set(key, task);
            return task;
        }

        function findTable(entry, name) {
            if (entry.tables.has(name)) return entry.tables.get(name);
            const lower = String(name || '').toLowerCase();
            let found = null;
            entry.tables.forEach(function (info) {
                if (!found && info.name.toLowerCase() === lower) found = info;
            });
            return found;
        }

        async function columnsOf(session, scope, tableName) {
            const entry = await loadTables(session, scope);
            const info = findTable(entry, tableName);
            const realName = info ? info.name : tableName;
            if (info && info.columns) return info.columns;
            const taskKey = keyOf(session, scope) + '|' + realName;
            if (columnTasks.has(taskKey)) return columnTasks.get(taskKey);
            const task = (async function () {
                let columns = [];
                try {
                    const data = await api.metaChildren(session.id,
                        folderRef(scope, 'columns', realName));
                    const nodes = data && Array.isArray(data.nodes) ? data.nodes : [];
                    columns = nodes.filter(function (node) { return node && node.name; })
                        .map(function (node) {
                            return { text: node.name, badge: node.badge || '' };
                        });
                } catch (error) {
                    console.warn('[silkit] 补全读取列失败', error);
                }
                if (info) info.columns = columns;
                columnTasks.delete(taskKey);
                return columns;
            })();
            columnTasks.set(taskKey, task);
            return task;
        }

        // 解析 FROM/JOIN/UPDATE/INTO 后面的表名与别名，供 `别名.` 与裸列名补全。
        // 注意：`[`"\w.$]+` 会把结尾的点号也吞掉，导致把列名当表名，这里改为分段匹配。
        // 注释里的 FROM 不参与解析，避免「注释提到别的表」造成串列。
        function tableRefs(text) {
            const map = new Map();
            const source = String(text || '')
                .replace(/\/\*[\s\S]*?\*\//g, ' ')
                .replace(/--[^\n]*/g, ' ');
            const pattern = /\b(?:FROM|JOIN|UPDATE|INTO)\s+([`"\[]?[A-Za-z_][\w$]*[`"\]]?(?:\s*\.\s*[`"\[]?[A-Za-z_][\w$]*[`"\]]?)*)(?:\s+(?:AS\s+)?([A-Za-z_]\w*))?/gi;
            let match = null;
            while ((match = pattern.exec(source)) !== null) {
                const parts = String(match[1]).split(/\s*\.\s*/).map(unquote);
                const name = parts[parts.length - 1];
                if (!name) continue;
                map.set(name.toLowerCase(), name);
                const alias = match[2] && !STOP_WORDS.has(match[2].toUpperCase())
                    ? match[2] : '';
                if (alias) map.set(alias.toLowerCase(), name);
            }
            return map;
        }

        function splitQualifier(raw) {
            return String(raw || '').split(/\s*\.\s*/)
                .map(function (part) { return unquote(part.trim()); })
                .filter(Boolean);
        }

        // 点号是否位于「表位置」（FROM / JOIN / UPDATE / INTO 之后）。
        // 表位置里的 `employees.` 通常是库限定符，应提示表名而不是列。
        function inTablePosition(prefix) {
            return /\b(?:FROM|JOIN|UPDATE|INTO)\s+(?:[`"\[]?[\w$]+[`"\]]?\s*\.\s*)*$/i.test(prefix);
        }

        // 严格解析：只有限定符能落到当前库里真实存在的表时才返回表名。
        // 找不到就返回 null，绝不退化成「拿限定符当表名」或混入其他表的列。
        function resolveTableStrict(refs, entry, parts) {
            const tail = parts[parts.length - 1] || '';
            const lower = tail.toLowerCase();
            const refName = refs.get(lower) || lower;
            const info = findTable(entry, refName);
            return info ? info.name : null;
        }

        // meta: { priority?: number, source?: string | (item) => string }
        // 候选自身也可带 badge / source；displayText 保留纯文本降级（旧版 show-hint 无 render）。
        function makeItems(items, type, prefix, meta) {
            const options = typeof meta === 'number' ? { priority: meta } : (meta || {});
            const lower = String(prefix || '').toLowerCase();
            const seen = new Set();
            const result = [];
            const cls = type === 'rareKeyword' ? 'keyword' : type;
            items.forEach(function (item) {
                const text = typeof item === 'string' ? item : item.text;
                if (!text) return;
                if (lower && text.toLowerCase().indexOf(lower) !== 0) return;
                const identity = cls + ':' + text.toLowerCase();
                if (seen.has(identity)) return;
                seen.add(identity);
                const badge = (typeof item === 'object' && item.badge) || '';
                const source = (typeof item === 'object' && item.source) ||
                    (typeof options.source === 'function' ? options.source(item) : options.source) || '';
                const displayParts = [text];
                if (badge) displayParts.push(badge);
                if (source) displayParts.push(source);
                result.push({
                    text: text,
                    displayText: displayParts.join('  '),
                    className: 'CodeMirror-hint-' + cls,
                    _priority: typeof options.priority === 'number' ? options.priority : PRIORITY[type],
                    _badge: badge,
                    _source: source,
                    render: renderHintItem
                });
            });
            return result;
        }

        function splitKeywords(keywords) {
            const common = [];
            const rare = [];
            keywords.forEach(function (word) {
                if (COMMON_KEYWORDS.has(String(word).toUpperCase())) common.push(word);
                else rare.push(word);
            });
            return { common: common, rare: rare };
        }

        function makeHint(session, getContext) {
            return function (editor) {
                const scope = scopeOf(session, getContext);
                const cursor = editor.getCursor();
                const line = editor.getLine(cursor.line) || '';
                const left = line.slice(0, cursor.ch);
                // `table.` / `alias.` / `db.table.` / `schema.table.`：最后一个点号触发补全。
                const qualified = /((?:[`"\[]?[A-Za-z_][\w$]*[`"\]]?\s*\.\s*)*[`"\[]?[A-Za-z_][\w$]*[`"\]]?)\s*\.\s*([\w$]*)$/.exec(left);
                const plain = /([A-Za-z0-9_$]+)$/.exec(left);
                const word = qualified ? qualified[2] : (plain ? plain[1] : '');
                const from = { line: cursor.line, ch: cursor.ch - word.length };
                const to = { line: cursor.line, ch: cursor.ch };

                return (async function () {
                    const entry = await loadTables(session, scope);
                    const before = editor.getRange({ line: 0, ch: 0 }, cursor);
                    // 解析光标所在的整条语句（含光标之后的 FROM），并用分号隔离前后语句。
                    const statement = statementAt(before, editor.getValue());
                    const refs = tableRefs(statement);
                    // 表候选：名称保持干净，所属库 / schema 与类型放到右侧。
                    const tableItems = function (targetEntry, targetScope) {
                        const src = scopeLabel(targetScope);
                        const items = [];
                        targetEntry.tables.forEach(function (info) { items.push(info); });
                        return items.map(function (info) {
                            return {
                                text: info.name,
                                badge: info.kind === 'view' ? 'view' : '',
                                source: src
                            };
                        });
                    };
                    // 列候选：右侧显示 `库.表` / `schema.表` 的所属路径。
                    const columnItems = function (columns, targetScope, tableName) {
                        const src = scopeLabel(targetScope) + '.' + tableName;
                        return columns.map(function (col) {
                            return Object.assign({}, col, { source: src });
                        });
                    };
                    let list = [];

                    if (qualified) {
                        const parts = splitQualifier(qualified[1]);
                        const head = parts[0] || '';
                        const prefix = left.slice(0, qualified.index);
                        const tablePos = inTablePosition(prefix);
                        const scopes = await loadScopes(session, scope);
                        const scopeHit = scopes.filter(function (name) {
                            return name.toLowerCase() === head.toLowerCase();
                        })[0] || '';

                        // FROM employees. 且 employees 是库/schema 名：应列出该库的表，而不是某张表的列。
                        if (tablePos && parts.length === 1 && scopeHit) {
                            const targetScope = scope.engine === 'sqlite' ? scope
                                : Object.assign({}, scope, scope.engine === 'postgres'
                                    ? { schema: scopeHit } : { database: scopeHit });
                            const targetEntry = await loadTables(session, targetScope);
                            list = makeItems(tableItems(targetEntry, targetScope), 'table', word);
                        } else {
                            // 多段限定符指定了别的库/schema 时，按目标作用域解析表。
                            let targetScope = scope;
                            if (parts.length > 1 && scopeHit && scope.engine !== 'sqlite') {
                                targetScope = Object.assign({}, scope, scope.engine === 'postgres'
                                    ? { schema: scopeHit } : { database: scopeHit });
                            }
                            const targetEntry = await loadTables(session, targetScope);
                            const tableName = resolveTableStrict(refs, targetEntry, parts);
                            if (tableName) {
                                const columns = await columnsOf(session, targetScope, tableName);
                                list = makeItems(columnItems(columns, targetScope, tableName),
                                    'column', word);
                            }
                            // 解析不到表时保持空列表：不混入关键字、表名或其他表的列。
                        }
                    } else {
                        const words = splitKeywords(entry.keywords);
                        list = list.concat(makeItems(words.common, 'keyword', word,
                            { source: 'keyword' }));
                        list = list.concat(makeItems(tableItems(entry, scope), 'table', word));

                        // 裸标识符只补「整条语句里 FROM/JOIN/UPDATE/INTO 出现过、且当前库中确实存在」的表的列。
                        // 语句已写出表时列就限定于这些表，不再混入对象树里选中的其它表。
                        const names = [];
                        refs.forEach(function (name) {
                            const info = findTable(entry, name);
                            if (info && names.indexOf(info.name) < 0) names.push(info.name);
                        });
                        if (!names.length && scope.table) {
                            const picked = findTable(entry, scope.table);
                            if (picked) names.push(picked.name);
                        }
                        const columnSets = await Promise.all(names.slice(0, 8).map(function (name) {
                            return columnsOf(session, scope, name).then(function (columns) {
                                return { name: name, columns: columns };
                            });
                        }));
                        columnSets.forEach(function (set) {
                            list = list.concat(makeItems(columnItems(set.columns, scope, set.name),
                                'column', word));
                        });
                        // 冷门关键字（函数、DDL 子句、引擎专属项）排到最后。
                        list = list.concat(makeItems(words.rare, 'rareKeyword', word,
                            { source: 'keyword' }));
                    }

                    list.sort(function (a, b) {
                        if (a._priority !== b._priority) return a._priority - b._priority;
                        return a.text.localeCompare(b.text);
                    });
                    return { list: list.slice(0, 200), from: from, to: to };
                })();
            };
        }

        function invalidate(sessionId) {
            const prefix = sessionId ? sessionId + '|' : '';
            [cache, pending, columnTasks, scopeCache, scopeTasks].forEach(function (map) {
                Array.from(map.keys()).forEach(function (key) {
                    if (!prefix || key.indexOf(prefix) === 0) map.delete(key);
                });
            });
        }

        return {
            ensure: function (session, context) { return loadTables(session, scopeOf(session, context)); },
            hint: makeHint,
            invalidate: invalidate,
            prefetch: function (session, context) {
                if (!session) return;
                loadTables(session, scopeOf(session, context)).catch(function (error) {
                    console.warn('[silkit] 补全预取失败', error);
                });
            }
        };
    }

    window.SilkitHint = { create: create };
})();
