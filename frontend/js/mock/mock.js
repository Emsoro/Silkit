// 浏览器开发/测试环境的假后端：模拟 TauriCPP 的 invoke/listen、SQLite 元数据与流式查询。
// 真机运行时本文件不会被使用（bridge.js 优先原生桥）。
(function () {
    'use strict';

    const listeners = new Map();
    const timers = new Map();
    const tasks = new Map();
    const sessions = new Map();
    const SETTINGS_KEY = 'silkit.settings';
    const PROFILES_KEY = 'silkit.profiles';
    const VERSION = '3.45.0';
    const SERVER_VERSIONS = { sqlite: '3.45.0', mysql: '8.4.0', postgres: '16.9' };
    function serverVersion(engine) { return SERVER_VERSIONS[engine] || VERSION; }
    function currentDbFor(profile) {
        return profile.engine === 'sqlite' ? 'main' : (profile.database || 'postgres');
    }

    const THEMES = [
        { id: 'bloom', name: '晨樱 Bloom', appearance: 'light', bg: '#FFF7F9', accent: '#C2255C' },
        { id: 'noir', name: '夜樱 Noir', appearance: 'dark', bg: '#17121A', accent: '#F06595' },
        { id: 'lilac', name: '薰衣草 Lilac', appearance: 'light', bg: '#FAF7FF', accent: '#6D4FC4' },
        { id: 'mocha', name: '奶咖 Mocha', appearance: 'light', bg: '#FBF7F3', accent: '#96563D' },
        { id: 'mint', name: '薄荷 Mint', appearance: 'light', bg: '#F5FBF9', accent: '#237668' }
    ];

    const DEFAULTS = {
        version: 1,
        ui: {
            theme: 'bloom', appearance: 'light', followSystem: false, reduceMotion: false,
            language: 'zh-CN', fontSize: 13, gridRowHeight: 26, sidebarWidth: 264,
            editorRatio: 0.44, restoreTabs: true, confirmDestructive: true
        },
        editor: { fontSize: 13, tabSize: 2, wordWrap: false, autocomplete: true, showLineNumbers: true },
        query: { maxRows: 5000, fetchSize: 500, timeoutMs: 0, autoLimit: true, historyLimit: 5000, safeMode: true },
        data: {
            pageSize: 500, nullDisplay: 'NULL', dateFormat: 'YYYY-MM-DD HH:mm:ss',
            binaryPreviewBytes: 65536, maxCellBytes: 262144
        },
        export: { defaultEncoding: 'utf-8', csvDelimiter: ',', includeHeader: true, nullLiteral: '' }
    };

    // ---------------------------------------------------------------- schema
    const USER_NAMES = [
        '林知夏', 'Alice Chen', 'Mika', '苏晚', 'Nora', '陈见月', '周以宁', '李澈',
        '王砚', '赵星野', '沈岚', '顾清和', '许南', '唐棠', '宋屿', '陆离'
    ];
    const STATUSES = ['active', 'active', 'pending', 'active', 'review', 'active'];

    function makeUsers(count) {
        const rows = [];
        for (let i = 1; i <= count; i += 1) {
            const name = USER_NAMES[(i - 1) % USER_NAMES.length];
            rows.push([
                String(i),
                name,
                i % 7 === 0 ? null : ('user' + i + '@example.com'),
                STATUSES[i % STATUSES.length],
                (60 + (i * 7) % 40 + 0.5).toFixed(1),
                '2026-11-' + String((i % 12) + 1).padStart(2, '0') + ' ' +
                    String((i * 3) % 24).padStart(2, '0') + ':' + String((i * 11) % 60).padStart(2, '0') + ':00',
                i % 9 === 0 ? 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==' : null
            ]);
        }
        return rows;
    }

    function makeOrders(count) {
        const rows = [];
        for (let i = 1; i <= count; i += 1) {
            rows.push([
                String(1000 + i),
                String((i % 16) + 1),
                (29.9 + (i * 13) % 400).toFixed(2),
                ['paid', 'paid', 'pending', 'refunded'][i % 4],
                '2026-10-' + String((i % 28) + 1).padStart(2, '0')
            ]);
        }
        return rows;
    }

    function makeProducts(count) {
        const rows = [];
        const names = ['机械键盘', '显示器', '人体工学椅', 'Type-C 扩展坞', '降噪耳机', '桌面灯'];
        for (let i = 1; i <= count; i += 1) {
            rows.push([
                String(i),
                names[(i - 1) % names.length] + ' ' + i,
                (99 + (i * 37) % 2000) + '.00',
                String((i * 17) % 120),
                i % 5 === 0 ? null : '在售'
            ]);
        }
        return rows;
    }

    const USER_COLUMNS = [
        { name: 'id', type: 'INTEGER', kind: 'int', nullable: false, primaryKey: true },
        { name: 'name', type: 'TEXT', kind: 'text', nullable: false, primaryKey: false },
        { name: 'email', type: 'TEXT', kind: 'text', nullable: true, primaryKey: false },
        { name: 'status', type: 'TEXT', kind: 'text', nullable: false, primaryKey: false },
        { name: 'score', type: 'REAL', kind: 'float', nullable: true, primaryKey: false },
        { name: 'created_at', type: 'DATETIME', kind: 'datetime', nullable: false, primaryKey: false },
        { name: 'avatar', type: 'BLOB', kind: 'binary', nullable: true, primaryKey: false }
    ];

    const TABLES = {
        users: {
            columns: USER_COLUMNS,
            rows: makeUsers(60),
            sql: 'CREATE TABLE users (\n  id INTEGER PRIMARY KEY,\n  name TEXT NOT NULL,\n  email TEXT,\n  status TEXT NOT NULL DEFAULT \'active\',\n  score REAL,\n  created_at DATETIME NOT NULL,\n  avatar BLOB\n);'
        },
        orders: {
            columns: [
                { name: 'id', type: 'INTEGER', kind: 'int', nullable: false, primaryKey: true },
                { name: 'user_id', type: 'INTEGER', kind: 'int', nullable: false, primaryKey: false },
                { name: 'total', type: 'DECIMAL(10,2)', kind: 'decimal', nullable: false, primaryKey: false },
                { name: 'status', type: 'TEXT', kind: 'text', nullable: false, primaryKey: false },
                { name: 'created_at', type: 'DATE', kind: 'date', nullable: false, primaryKey: false }
            ],
            rows: makeOrders(42),
            sql: 'CREATE TABLE orders (\n  id INTEGER PRIMARY KEY,\n  user_id INTEGER NOT NULL REFERENCES users(id),\n  total DECIMAL(10,2) NOT NULL,\n  status TEXT NOT NULL,\n  created_at DATE NOT NULL\n);'
        },
        products: {
            columns: [
                { name: 'id', type: 'INTEGER', kind: 'int', nullable: false, primaryKey: true },
                { name: 'name', type: 'TEXT', kind: 'text', nullable: false, primaryKey: false },
                { name: 'price', type: 'DECIMAL(10,2)', kind: 'decimal', nullable: false, primaryKey: false },
                { name: 'stock', type: 'INTEGER', kind: 'int', nullable: false, primaryKey: false },
                { name: 'state', type: 'TEXT', kind: 'text', nullable: true, primaryKey: false }
            ],
            rows: makeProducts(24),
            sql: 'CREATE TABLE products (\n  id INTEGER PRIMARY KEY,\n  name TEXT NOT NULL,\n  price DECIMAL(10,2) NOT NULL,\n  stock INTEGER NOT NULL,\n  state TEXT\n);'
        }
    };

    const VIEWS = {
        active_users: {
            columns: USER_COLUMNS,
            rows: TABLES.users.rows.filter(function (row) { return row[3] === 'active'; }),
            sql: 'CREATE VIEW active_users AS\nSELECT * FROM users WHERE status = \'active\';'
        }
    };

    const INDEXES = [
        { name: 'idx_users_email', table: 'users', unique: true, columns: ['email'] },
        { name: 'idx_users_status', table: 'users', unique: false, columns: ['status'] },
        { name: 'idx_orders_user', table: 'orders', unique: false, columns: ['user_id'] }
    ];

    const TRIGGERS = [
        {
            name: 'trg_users_touch',
            table: 'users',
            sql: 'CREATE TRIGGER trg_users_touch AFTER UPDATE ON users\nBEGIN\n  SELECT 1;\nEND;'
        }
    ];

    const FOREIGN_KEYS = [
        { id: 0, seq: 0, table: 'users', from: 'user_id', to: 'id', onUpdate: 'NO ACTION', onDelete: 'NO ACTION', match: 'NONE' }
    ];

    let profileSeed = [{
        id: 'c_demo',
        name: '示例库 sample.db',
        engine: 'sqlite',
        file: 'C:\\data\\sample.db',
        readOnly: false,
        options: { busyTimeoutMs: 5000, foreignKeys: true, journalMode: '' },
        createdAt: '2026-11-01T08:00:00Z',
        lastUsedAt: '2026-11-12T09:00:00Z'
    }];

    function loadProfiles() {
        try {
            const raw = localStorage.getItem(PROFILES_KEY);
            if (raw) return JSON.parse(raw);
        } catch (error) { /* ignore */ }
        return clone(profileSeed);
    }

    function saveProfiles(list) {
        try { localStorage.setItem(PROFILES_KEY, JSON.stringify(list)); } catch (error) { /* ignore */ }
    }

    // ---------------------------------------------------------------- helpers
    function clone(value) { return JSON.parse(JSON.stringify(value)); }

    function mergeJson(base, patch) {
        if (!patch || typeof patch !== 'object' || Array.isArray(patch) ||
            !base || typeof base !== 'object' || Array.isArray(base)) return clone(patch);
        Object.keys(patch).forEach(function (key) {
            if (base[key] && typeof base[key] === 'object' && !Array.isArray(base[key]) &&
                patch[key] && typeof patch[key] === 'object' && !Array.isArray(patch[key])) {
                base[key] = mergeJson(base[key], patch[key]);
            } else {
                base[key] = clone(patch[key]);
            }
        });
        return base;
    }

    function loadSettings() {
        try {
            const raw = localStorage.getItem(SETTINGS_KEY);
            if (raw) return mergeJson(clone(DEFAULTS), JSON.parse(raw));
        } catch (e) { /* ignore */ }
        return clone(DEFAULTS);
    }

    function saveSettings(settings) {
        try { localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings)); } catch (e) { /* ignore */ }
    }

    function emit(event, data) {
        const list = listeners.get(event) || [];
        list.slice().forEach(function (cb) {
            try { cb(data); } catch (e) { console.error('[mock] listener', e); }
        });
    }

    function listen(event, cb) {
        if (!listeners.has(event)) listeners.set(event, []);
        listeners.get(event).push(cb);
        return function () {
            listeners.set(event, (listeners.get(event) || []).filter(function (fn) { return fn !== cb; }));
        };
    }

    function later(fn, ms) { return setTimeout(fn, ms || 0); }

    function reply(reqId, data) {
        later(function () {
            emit('ipc.event', { reqId: reqId, type: 'result', data: data });
        }, 20);
    }

    function fail(reqId, error) {
        later(function () {
            emit('ipc.event', { reqId: reqId, type: 'error', error: error });
        }, 20);
    }

    function progress(reqId, data) {
        later(function () {
            emit('ipc.event', { reqId: reqId, type: 'progress', data: data });
        }, 10);
    }

    function event(reqId, type, data) {
        emit('ipc.event', { reqId: reqId, type: type, data: data });
    }

    function node(kind, name, database, hasChildren, badge, table) {
        const item = {
            kind: kind,
            name: name,
            database: database || 'main',
            schema: '',
            hasChildren: !!hasChildren,
            expandable: !!hasChildren
        };
        if (badge) item.badge = badge;
        if (table) item.table = table;
        return item;
    }

    function folder(name, database, badge) {
        return node('folder', name, database, true, badge);
    }

    function indexNodes(database, table) {
        return INDEXES.filter(function (index) {
            return !table || index.table === table;
        }).map(function (index) {
            return node('index', index.name, database, false, index.unique ? '唯一' : '', index.table);
        });
    }

    function triggerNodes(database, table) {
        return TRIGGERS.filter(function (trigger) {
            return !table || trigger.table === table;
        }).map(function (trigger) {
            return node('trigger', trigger.name, database, false, trigger.table);
        });
    }

    function metadataChildren(parent) {
        const kind = parent && parent.kind || 'connection';
        const database = parent && parent.database || 'main';
        const name = parent && parent.name || '';
        const table = parent && parent.table || '';
        const root = sessions.get(parent && parent.sessionId);
        const file = root && root.profile ? root.profile.file : 'C:\\data\\sample.db';

        if (kind === 'connection' || !kind) {
            return [
                node('database', 'main', 'main', true, file),
                node('database', 'temp', 'temp', true, '内存')
            ];
        }
        if (kind === 'database') {
            const tableCount = database === 'main' ? Object.keys(TABLES).length : 0;
            const viewCount = database === 'main' ? Object.keys(VIEWS).length : 0;
            const indexCount = database === 'main' ? INDEXES.length : 0;
            const triggerCount = database === 'main' ? TRIGGERS.length : 0;
            return [
                folder('tables', database, tableCount ? String(tableCount) : ''),
                folder('views', database, viewCount ? String(viewCount) : ''),
                folder('indexes', database, indexCount ? String(indexCount) : ''),
                folder('triggers', database, triggerCount ? String(triggerCount) : '')
            ];
        }
        if (kind === 'folder') {
            if (table) {
                if (name === 'columns') {
                    const def = TABLES[table] || VIEWS[table];
                    return (def ? def.columns : []).map(function (column) {
                        return node('column', column.name, database, false,
                            column.type + (column.primaryKey ? ' · PK' : ''), table);
                    });
                }
                if (name === 'indexes') return indexNodes(database, table);
                if (name === 'foreign_keys') {
                    return FOREIGN_KEYS.map(function (fk) {
                        const item = node('foreign_key', fk.from, database, false,
                            '→ ' + fk.table + '.' + fk.to, table);
                        item.column = fk.from;
                        item.refTable = fk.table;
                        item.refColumn = fk.to;
                        return item;
                    });
                }
                if (name === 'triggers') return triggerNodes(database, table);
            }
            if (database !== 'main') return [];
            if (name === 'tables') {
                return Object.keys(TABLES).map(function (key) { return node('table', key, database, true); });
            }
            if (name === 'views') {
                return Object.keys(VIEWS).map(function (key) { return node('view', key, database, true); });
            }
            if (name === 'indexes') return indexNodes(database, '');
            if (name === 'triggers') return triggerNodes(database, '');
            return [];
        }
        if (kind === 'table' || kind === 'view') {
            const items = [folder('columns', database, '列')];
            if (kind === 'table') {
                items.push(folder('indexes', database, '索引'));
                items.push(folder('foreign_keys', database, '外键'));
            }
            items.push(folder('triggers', database, '触发器'));
            return items;
        }
        return [];
    }

    function metadataDetail(ref) {
        const def = TABLES[ref.name] || VIEWS[ref.name];
        if (!def) throw { code: 'not_found', message: '对象不存在: ' + ref.name };
        const columns = def.columns.map(function (column, index) {
            return {
                name: column.name,
                type: column.type,
                kind: column.kind,
                notNull: column.nullable === false,
                default: null,
                primaryKey: !!column.primaryKey,
                hidden: 0,
                ordinal: index
            };
        });
        const indexes = ref.kind === 'view' ? [] : INDEXES.filter(function (index) {
            return index.table === ref.name;
        }).map(function (index) {
            return {
                name: index.name, unique: index.unique, origin: 'c', partial: false,
                columns: index.columns
            };
        });
        return {
            database: ref.database || 'main',
            name: ref.name,
            columns: columns,
            primaryKey: columns.filter(function (c) { return c.primaryKey; }).map(function (c) { return c.name; }),
            indexes: indexes,
            foreignKeys: ref.kind === 'view' ? [] : FOREIGN_KEYS,
            triggers: TRIGGERS.filter(function (trigger) { return trigger.table === ref.name; })
                .map(function (trigger) { return { name: trigger.name, sql: trigger.sql }; }),
            sql: def.sql
        };
    }

    function ddlFor(ref) {
        if (ref.kind === 'table' || ref.kind === 'view') {
            const def = TABLES[ref.name] || VIEWS[ref.name];
            if (!def) throw { code: 'not_found', message: '对象不存在: ' + ref.name };
            return { sql: def.sql, warnings: [] };
        }
        if (ref.kind === 'index') {
            const index = INDEXES.find(function (item) { return item.name === ref.name; });
            return {
                sql: index ? 'CREATE ' + (index.unique ? 'UNIQUE ' : '') + 'INDEX ' + index.name +
                    ' ON ' + index.table + ' (' + index.columns.join(', ') + ');' : '',
                warnings: []
            };
        }
        if (ref.kind === 'trigger') {
            const trigger = TRIGGERS.find(function (item) { return item.name === ref.name; });
            return { sql: trigger ? trigger.sql : '', warnings: [] };
        }
        throw { code: 'bad_args', message: '该对象没有 DDL: ' + ref.kind };
    }

    // ---------------------------------------------------------------- 查询
    function splitSql(sql) {
        const parts = [];
        let start = 0;
        let quote = null;
        const text = String(sql || '');
        for (let i = 0; i < text.length; i += 1) {
            const ch = text[i];
            if (quote) {
                if (ch === quote) {
                    if (text[i + 1] === quote) i += 1;
                    else quote = null;
                }
                continue;
            }
            if (ch === "'" || ch === '"' || ch === '`') {
                quote = ch;
            } else if (ch === ';') {
                const part = text.slice(start, i + 1);
                if (part.trim()) parts.push({ text: part.trim(), offset: start });
                start = i + 1;
            }
        }
        const tail = text.slice(start);
        if (tail.trim()) parts.push({ text: tail.trim(), offset: start });
        return parts;
    }

    function resultFor(sql) {
        const lower = sql.toLowerCase();
        function fromTable(name) {
            const quoted = '[`"\\[]?' + name + '[`"\\]]?';
            return new RegExp('\\b(from|join)\\s+' + quoted + '\\b').test(lower);
        }
        let table = null;
        if (fromTable('users')) table = 'users';
        else if (fromTable('orders')) table = 'orders';
        else if (fromTable('products')) table = 'products';
        else if (fromTable('active_users')) table = 'active_users';

        if (/count\s*\(/.test(lower)) {
            const def = table ? (TABLES[table] || VIEWS[table]) : null;
            return {
                columns: [{ name: 'count(*)', type: '', kind: 'int', nullable: false, primaryKey: false }],
                rows: [[String(def ? def.rows.length : 0)]],
                batchSize: 10
            };
        }

        const def = table ? (TABLES[table] || VIEWS[table]) : null;
        if (!def) {
            return {
                columns: [
                    { name: 'sqlite_version()', type: '', kind: 'text', nullable: false, primaryKey: false },
                    { name: 'now', type: '', kind: 'text', nullable: false, primaryKey: false }
                ],
                rows: [[VERSION, new Date().toISOString()]],
                batchSize: 10
            };
        }

        let rows = def.rows;
        const limit = /limit\s+(\d+)/.exec(lower);
        if (limit) rows = rows.slice(0, Number(limit[1]));
        const whereId = /where\s+id\s*=\s*(\d+)/.exec(lower);
        if (whereId) {
            const id = Number(whereId[1]);
            rows = rows.filter(function (row) { return Number(row[0]) === id; });
        }
        return { columns: def.columns, rows: rows, batchSize: 20 };
    }

    function startQuery(reqId, args) {
        const sql = String(args.sql || '');
        const session = sessions.get(args.sessionId);
        if (!session) {
            return fail(reqId, { code: 'session_not_found', message: '连接已关闭或不存在' });
        }
        if (!sql.trim()) {
            return fail(reqId, { code: 'bad_args', message: 'SQL 不能为空' });
        }

        const statements = splitSql(sql);
        const started = Date.now();
        const summaries = [];
        const task = { cancelled: false, timer: 0, reqId: reqId };
        tasks.set(reqId, task);
        let index = 0;

        function finish() {
            if (task.cancelled) return;
            tasks.delete(reqId);
            reply(reqId, { statements: summaries, elapsedMs: Date.now() - started });
        }

        function nextStatement() {
            if (task.cancelled) return;
            if (index >= statements.length) {
                finish();
                return;
            }
            if (/raise_error|\bmissing\b/i.test(statements[index].text)) {
                tasks.delete(reqId);
                fail(reqId, {
                    code: 1,
                    message: 'no such table: missing',
                    statementIndex: index,
                    offset: statements[index].offset + 14,
                    sql: statements[index].text,
                    elapsedMs: Date.now() - started
                });
                return;
            }

            const statement = statements[index];
            event(reqId, 'statement', {
                index: index,
                total: statements.length,
                offset: statement.offset,
                sql: statement.text
            });

            const lower = statement.text.toLowerCase();
            const isSelect = /^\s*(select|with|pragma|explain)\b/.test(lower);
            if (!isSelect) {
                let affected = 0;
                if (/^\s*insert/.test(lower)) affected = 1;
                else if (/^\s*(update|delete)/.test(lower)) affected = 3;
                summaries.push({
                    index: index, rows: 0, affected: affected,
                    elapsedMs: 8 + index * 2, truncated: false, columns: 0, cancelled: false
                });
                index += 1;
                task.timer = later(nextStatement, 30);
                return;
            }

            const result = resultFor(statement.text);
            let offset = 0;
            let seq = 0;

            function nextBatch() {
                if (task.cancelled) return;
                if (offset >= result.rows.length) {
                    summaries.push({
                        index: index, rows: result.rows.length, affected: 0,
                        elapsedMs: 12 + index * 4, truncated: false,
                        columns: result.columns.length, cancelled: false
                    });
                    index += 1;
                    task.timer = later(nextStatement, 10);
                    return;
                }
                const slice = result.rows.slice(offset, offset + result.batchSize);
                const payload = { statement: index, seq: seq, rows: slice };
                if (offset === 0) payload.columns = result.columns;
                event(reqId, 'batch', payload);
                seq += 1;
                offset += slice.length;
                task.timer = later(nextBatch, 45);
            }

            nextBatch();
        }

        nextStatement();
    }

    // ---------------------------------------------------------------- 命令
    const mockFiles = new Map();
    mockFiles.set('C:\\mock\\sample.sql', '-- Mock 示例 SQL\nSELECT * FROM users;\n');

    function sessionEngine(sessionId) {
        const session = sessions.get(sessionId);
        return session && session.profile ? (session.profile.engine || 'sqlite') : 'sqlite';
    }

    // ---------------------------------------------------------------- M5 表数据（mock）
    function quoteId(name) {
        return '"' + String(name).replace(/"/g, '""') + '"';
    }

    function tableEntry(ref) {
        const name = ref && ref.name;
        const fromView = !!(ref && ref.kind === 'view') || (!TABLES[name] && !!VIEWS[name]);
        const def = fromView ? VIEWS[name] : TABLES[name];
        if (!def) throw { code: 'not_found', message: '对象不存在: ' + name };
        return { name: name, def: def, isView: fromView };
    }

    function ensureRowIds(entry) {
        if (!Array.isArray(entry.def._rowids) ||
            entry.def._rowids.length !== entry.def.rows.length) {
            entry.def._rowids = entry.def.rows.map(function (_, index) { return index + 1; });
            entry.def._nextRowId = entry.def._rowids.reduce(function (max, id) {
                return Math.max(max, id);
            }, 0) + 1;
        }
    }

    function tableIdentity(entry) {
        if (entry.isView) return { type: 'none', columns: [] };
        const pk = entry.def.columns.filter(function (column) {
            return !!column.primaryKey;
        }).map(function (column) { return column.name; });
        if (pk.length) return { type: 'pk', columns: pk };
        for (let i = 0; i < INDEXES.length; i += 1) {
            const index = INDEXES[i];
            if (!index.unique || index.table !== entry.name || index.columns.length !== 1) continue;
            const column = entry.def.columns.find(function (item) {
                return item.name === index.columns[0];
            });
            if (column && column.nullable === false) {
                return { type: 'unique', columns: index.columns.slice() };
            }
        }
        return { type: 'rowid', columns: ['rowid'] };
    }

    function editableInfo(entry, sessionId) {
        const session = sessions.get(sessionId);
        if (session && session.readOnly) {
            return { editable: false, reason: '连接为只读模式' };
        }
        if (entry.isView) {
            return { editable: false, reason: '视图没有可用于定位行的主键/唯一索引/rowid' };
        }
        const identity = tableIdentity(entry);
        if (identity.type === 'none') {
            return { editable: false, reason: '该表没有可用于定位行的主键/唯一索引/rowid' };
        }
        return { editable: true, reason: '' };
    }

    function matchFilter(value, filter) {
        const op = String(filter.op || '=').toUpperCase();
        if (op === 'IS NULL') return value == null;
        if (op === 'IS NOT NULL') return value != null;
        if (value == null) return false;
        const text = String(value);
        if (op === 'IN') {
            const list = Array.isArray(filter.value)
                ? filter.value
                : String(filter.value == null ? '' : filter.value).split(',');
            return list.some(function (item) { return String(item) === text; });
        }
        if (op === 'LIKE') {
            const needle = String(filter.value == null ? '' : filter.value)
                .replace(/[%_]/g, '').toLowerCase();
            return text.toLowerCase().indexOf(needle) >= 0;
        }
        const right = filter.value == null ? '' : String(filter.value);
        const leftNumber = Number(text);
        const rightNumber = Number(right);
        const numeric = text.trim() !== '' && right.trim() !== '' &&
            isFinite(leftNumber) && isFinite(rightNumber);
        const left = numeric ? leftNumber : text;
        const rhs = numeric ? rightNumber : right;
        switch (op) {
            case '=':
            case '==':
                return numeric ? left === rhs : text === right;
            case '<>':
            case '!=':
                return numeric ? left !== rhs : text !== right;
            case '>': return left > rhs;
            case '>=': return left >= rhs;
            case '<': return left < rhs;
            case '<=': return left <= rhs;
            default: return true;
        }
    }

    function tableEntries(entry, args) {
        ensureRowIds(entry);
        const columns = entry.def.columns;
        const filters = Array.isArray(args.filters) ? args.filters : [];
        const search = String(args.search || '').toLowerCase();
        let entries = entry.def.rows.map(function (values, index) {
            return { values: values, id: entry.def._rowids[index], index: index };
        });
        if (search) {
            entries = entries.filter(function (item) {
                return item.values.some(function (value) {
                    return value != null && String(value).toLowerCase().indexOf(search) >= 0;
                });
            });
        }
        filters.forEach(function (filter) {
            if (!filter || !filter.column) return;
            const index = columns.findIndex(function (column) {
                return column.name === filter.column;
            });
            if (index < 0) return;
            entries = entries.filter(function (item) {
                return matchFilter(item.values[index], filter);
            });
        });
        if (args.order && args.order.column) {
            const index = columns.findIndex(function (column) {
                return column.name === args.order.column;
            });
            if (index >= 0) {
                const desc = !!args.order.desc;
                entries.sort(function (left, right) {
                    const a = left.values[index];
                    const b = right.values[index];
                    if (a == null && b == null) return 0;
                    if (a == null) return desc ? 1 : -1;
                    if (b == null) return desc ? -1 : 1;
                    const na = Number(a);
                    const nb = Number(b);
                    let order;
                    if (isFinite(na) && isFinite(nb) && String(a).trim() !== '' &&
                        String(b).trim() !== '') {
                        order = na - nb;
                    } else {
                        order = String(a).localeCompare(String(b));
                    }
                    return desc ? -order : order;
                });
            }
        }
        return entries;
    }

    function rowKeysFor(entry, identity, item) {
        const keys = {};
        if (identity.type === 'rowid') {
            keys.rowid = String(item.id);
            return keys;
        }
        identity.columns.forEach(function (name) {
            const index = entry.def.columns.findIndex(function (column) {
                return column.name === name;
            });
            keys[name] = index >= 0 && item.values[index] != null
                ? String(item.values[index])
                : '';
        });
        return keys;
    }

    function sqlLiteral(value) {
        if (value == null) return 'NULL';
        const text = String(value);
        if (/^-?\d+(\.\d+)?$/.test(text)) return text;
        return "'" + text.replace(/'/g, "''") + "'";
    }

    function tableSqlPreview(entry, identity, changes) {
        const preview = [];
        (changes.inserts || []).forEach(function (insert) {
            const values = insert.values || {};
            const names = entry.def.columns.filter(function (column) {
                return Object.prototype.hasOwnProperty.call(values, column.name);
            }).map(function (column) { return column.name; });
            if (!names.length) {
                preview.push('INSERT INTO ' + quoteId(entry.name) + ' DEFAULT VALUES');
                return;
            }
            preview.push('INSERT INTO ' + quoteId(entry.name) + ' (' +
                names.map(quoteId).join(', ') + ') VALUES (' +
                names.map(function (name) { return sqlLiteral(values[name]); }).join(', ') + ')');
        });
        (changes.updates || []).forEach(function (update) {
            const keys = update.keys || {};
            const values = update.changes || {};
            const names = entry.def.columns.filter(function (column) {
                return Object.prototype.hasOwnProperty.call(values, column.name);
            }).map(function (column) { return column.name; });
            const where = Object.keys(keys).map(function (name) {
                return quoteId(name) + ' = ' + sqlLiteral(keys[name]);
            }).join(' AND ');
            preview.push('UPDATE ' + quoteId(entry.name) + ' SET ' +
                names.map(function (name) {
                    return quoteId(name) + ' = ' + sqlLiteral(values[name]);
                }).join(', ') + ' WHERE ' + where);
        });
        (changes.deletes || []).forEach(function (remove) {
            const keys = remove.keys || {};
            const where = Object.keys(keys).map(function (name) {
                return quoteId(name) + ' = ' + sqlLiteral(keys[name]);
            }).join(' AND ');
            preview.push('DELETE FROM ' + quoteId(entry.name) + ' WHERE ' + where);
        });
        return preview;
    }

    function rowMatchesKeys(entry, identity, item, keys) {
        if (!keys || !Object.keys(keys).length) return false;
        if (identity.type === 'rowid') return String(item.id) === String(keys.rowid);
        return identity.columns.every(function (name) {
            const index = entry.def.columns.findIndex(function (column) {
                return column.name === name;
            });
            if (index < 0) return false;
            const value = item.values[index];
            return (value == null ? '' : String(value)) ===
                (keys[name] == null ? '' : String(keys[name]));
        });
    }

    function applyTableChanges(entry, identity, changes) {
        const snapshotRows = clone(entry.def.rows);
        const snapshotIds = entry.def._rowids ? entry.def._rowids.slice() : null;
        const snapshotNext = entry.def._nextRowId;
        const preview = tableSqlPreview(entry, identity, changes);
        let affected = 0;
        try {
            (changes.inserts || []).forEach(function (insert, index) {
                const values = insert.values || {};
                const row = entry.def.columns.map(function (column) {
                    return Object.prototype.hasOwnProperty.call(values, column.name)
                        ? values[column.name]
                        : null;
                });
                entry.def.columns.forEach(function (column, columnIndex) {
                    if (column.nullable !== false || column.primaryKey) return;
                    if (row[columnIndex] == null) {
                        throw {
                            code: 'not_null',
                            message: 'NOT NULL constraint failed: ' + entry.name + '.' + column.name,
                            kind: 'insert', clientIndex: insert.clientIndex || index,
                            rolledBack: true
                        };
                    }
                });
                entry.def.columns.forEach(function (column, columnIndex) {
                    if (!column.primaryKey) return;
                    const exists = entry.def.rows.some(function (existing) {
                        return String(existing[columnIndex]) === String(row[columnIndex]);
                    });
                    if (exists) {
                        throw {
                            code: 'constraint',
                            message: 'UNIQUE constraint failed: ' + entry.name + '.' + column.name,
                            kind: 'insert', clientIndex: insert.clientIndex || index,
                            rolledBack: true
                        };
                    }
                });
                entry.def.rows.push(row);
                entry.def._rowids.push(entry.def._nextRowId || (entry.def._rowids.length + 1));
                entry.def._nextRowId = (entry.def._nextRowId || 1) + 1;
                affected += 1;
            });
            (changes.updates || []).forEach(function (update) {
                const keys = update.keys || {};
                const values = update.changes || {};
                const target = tableEntries(entry, {}).find(function (item) {
                    return rowMatchesKeys(entry, identity, item, keys);
                });
                if (!target) return;
                entry.def.columns.forEach(function (column, columnIndex) {
                    if (!Object.prototype.hasOwnProperty.call(values, column.name)) return;
                    if (column.generated) return;
                    target.values[columnIndex] = values[column.name];
                });
                affected += 1;
            });
            (changes.deletes || []).forEach(function (remove) {
                const keys = remove.keys || {};
                const target = tableEntries(entry, {}).find(function (item) {
                    return rowMatchesKeys(entry, identity, item, keys);
                });
                if (!target) return;
                entry.def.rows.splice(target.index, 1);
                entry.def._rowids.splice(target.index, 1);
                affected += 1;
            });
        } catch (error) {
            entry.def.rows = snapshotRows;
            if (snapshotIds) entry.def._rowids = snapshotIds;
            entry.def._nextRowId = snapshotNext;
            throw error;
        }
        return { affected: affected, preview: preview };
    }

    function startTask(args) {
        const reqId = args.reqId;
        const cmd = args.cmd;
        const a = args.args || {};

        switch (cmd) {
            case 'app.info':
                reply(reqId, {
                    ok: true, name: 'Silkit', version: '0.1.0', build: 'browser mock',
                    platform: 'web', engines: ['sqlite', 'mysql', 'postgres'],
                    plannedEngines: [], dataDir: '(browser mock)'
                });
                return { ok: true, reqId };

            case 'app.ping': {
                const steps = Math.max(1, Math.min(10, Number(a.steps) || 3));
                const delay = Math.max(0, Math.min(2000, Number(a.delayMs) || 180));
                const started = Date.now();
                const task = { cancelled: false, timer: 0, ping: true };
                tasks.set(reqId, task);
                let step = 1;
                function tick() {
                    if (task.cancelled) return;
                    if (step <= steps) {
                        emit('ipc.event', {
                            reqId: reqId, type: 'progress',
                            data: { step: step, total: steps, message: '异步通道正常 ' + step }
                        });
                        step += 1;
                        task.timer = later(tick, delay);
                    } else {
                        tasks.delete(reqId);
                        emit('ipc.event', {
                            reqId: reqId, type: 'result',
                            data: { pong: true, steps: steps, elapsedMs: Date.now() - started }
                        });
                    }
                }
                task.timer = later(tick, Math.min(120, delay));
                return { ok: true, reqId };
            }

            case 'settings.get':
                reply(reqId, { settings: loadSettings() });
                return { ok: true, reqId };

            case 'settings.set': {
                const settings = mergeJson(loadSettings(), a.patch || {});
                saveSettings(settings);
                reply(reqId, { settings: settings });
                return { ok: true, reqId };
            }

            case 'theme.list':
                reply(reqId, { themes: clone(THEMES) });
                return { ok: true, reqId };

            case 'theme.set': {
                const theme = THEMES.find(function (t) { return t.id === a.theme; }) || THEMES[0];
                if (a.persist !== false) {
                    const settings = loadSettings();
                    settings.ui.theme = theme.id;
                    settings.ui.appearance = theme.appearance;
                    settings.ui.followSystem = false;
                    saveSettings(settings);
                }
                reply(reqId, { ok: true, theme: theme.id, windowBg: theme.bg });
                return { ok: true, reqId };
            }

            case 'app.log':
                console.log('[silkit]', a.level || 'info', a.message || '');
                reply(reqId, { ok: true });
                return { ok: true, reqId };

            case 'app.quit':
                window.close();
                return { ok: true, reqId };

            case 'app.pick_file': {
                const name = a.save ? (a.defaultName || 'silkit.db') : 'sample.sqlite';
                reply(reqId, { ok: true, path: 'C:\\data\\' + name });
                return { ok: true, reqId };
            }

            // ---------------------------------------------------- 连接
            case 'conn.list': {
                const list = loadProfiles();
                reply(reqId, { connections: list });
                return { ok: true, reqId };
            }

            case 'conn.save': {
                const profiles = loadProfiles();
                const profile = clone(a.profile || {});
                let id = profile.id || '';
                if (id) {
                    const index = profiles.findIndex(function (item) { return item.id === id; });
                    if (index >= 0) {
                        profiles[index] = mergeJson(profiles[index], profile);
                    } else {
                        profiles.push(profile);
                    }
                } else {
                    id = 'c_' + Date.now().toString(36);
                    profile.id = id;
                    profile.createdAt = new Date().toISOString();
                    profiles.push(profile);
                }
                saveProfiles(profiles);
                reply(reqId, { id: id });
                return { ok: true, reqId };
            }

            case 'conn.delete': {
                const profiles = loadProfiles().filter(function (item) { return item.id !== a.id; });
                saveProfiles(profiles);
                reply(reqId, { ok: true });
                return { ok: true, reqId };
            }

            case 'conn.test': {
                const profile = a.profile || {};
                const engine = profile.engine || 'sqlite';
                if (engine === 'sqlite' && !profile.file) {
                    fail(reqId, { code: 'bad_args', message: 'SQLite 文件路径不能为空' });
                    return { ok: true, reqId };
                }
                if (engine !== 'sqlite' && !profile.user) {
                    fail(reqId, { code: 'bad_args', message: '数据库用户名不能为空' });
                    return { ok: true, reqId };
                }
                reply(reqId, {
                    ok: true, serverVersion: serverVersion(engine),
                    currentDb: currentDbFor(profile), elapsedMs: 18
                });
                return { ok: true, reqId };
            }

            case 'conn.open': {
                const profile = clone(a.profile || {});
                const engine = profile.engine || 'sqlite';
                const id = 's_' + Date.now().toString(36);
                sessions.set(id, {
                    profile: profile, serverVersion: serverVersion(engine),
                    currentDb: currentDbFor(profile)
                });
                reply(reqId, {
                    sessionId: id,
                    serverVersion: serverVersion(engine),
                    currentDb: currentDbFor(profile),
                    capabilities: {
                        databases: true, schemas: engine === 'postgres',
                        views: true, routines: engine !== 'sqlite',
                        triggers: true, events: engine === 'mysql',
                        sequences: engine === 'postgres', explain: true, cancel: true
                    },
                    profile: profile
                });
                return { ok: true, reqId };
            }

            case 'conn.close':
                sessions.delete(a.sessionId);
                emit('session.closed', { sessionId: a.sessionId, reason: 'closed' });
                reply(reqId, { ok: true });
                return { ok: true, reqId };

            case 'conn.reconnect': {
                const current = sessions.get(a.sessionId);
                if (!current) {
                    fail(reqId, { code: 'session_not_found', message: '会话不存在' });
                    return { ok: true, reqId };
                }
                reply(reqId, {
                    sessionId: a.sessionId, serverVersion: VERSION,
                    currentDb: current.currentDb || 'main',
                    capabilities: { databases: true, views: true, explain: true, cancel: true }
                });
                return { ok: true, reqId };
            }

            case 'conn.set_database': {
                const session = sessions.get(a.sessionId);
                const engine = session && session.profile ? session.profile.engine : 'sqlite';
                if (engine === 'sqlite' && a.database !== 'main' && a.database !== 'temp') {
                    fail(reqId, { code: 'database_not_attached', message: '数据库未附加：' + a.database });
                    return { ok: true, reqId };
                }
                if (session) session.currentDb = a.database;
                reply(reqId, { ok: true, currentDb: a.database });
                return { ok: true, reqId };
            }

            case 'conn.set_readonly': {
                const target = sessions.get(a.sessionId);
                if (target) target.readOnly = !!a.readOnly;
                reply(reqId, { ok: true, readOnly: !!a.readOnly });
                return { ok: true, reqId };
            }

            case 'conn.ping':
                reply(reqId, { ok: true, latencyMs: 4, error: {} });
                return { ok: true, reqId };

            case 'conn.stats': {
                const session = sessions.get(a.sessionId);
                const engine = session && session.profile ? session.profile.engine : 'sqlite';
                reply(reqId, {
                    threadId: 1, serverVersion: serverVersion(engine),
                    currentDb: session ? session.currentDb || currentDbFor(session.profile) : 'main',
                    tx: 'autocommit', charset: engine === 'mysql' ? 'utf8mb4' : 'UTF8',
                    timezone: engine === 'sqlite' ? '' : 'UTC', capabilities: {}
                });
                return { ok: true, reqId };
            }

            // ---------------------------------------------------- 元数据
            case 'meta.children':
                try {
                    const parent = clone(a.parent || {});
                    parent.sessionId = a.sessionId;
                    reply(reqId, { nodes: metadataChildren(parent) });
                } catch (error) {
                    fail(reqId, error);
                }
                return { ok: true, reqId };

            case 'meta.detail':
                try {
                    reply(reqId, metadataDetail(a.ref || {}));
                } catch (error) {
                    fail(reqId, error);
                }
                return { ok: true, reqId };

            case 'meta.ddl':
                try {
                    reply(reqId, ddlFor(a.ref || {}));
                } catch (error) {
                    fail(reqId, error);
                }
                return { ok: true, reqId };

            case 'meta.refresh':
                reply(reqId, { ok: true, epoch: 1 });
                return { ok: true, reqId };

            case 'meta.search': {
                const keyword = String(a.keyword || '').toLowerCase();
                const limit = Math.max(1, Math.min(1000, Number(a.limit) || 200));
                const kinds = Array.isArray(a.kinds) ? a.kinds : [];
                const candidates = [];
                Object.keys(TABLES).forEach(function (name) {
                    candidates.push({ kind: 'table', name: name });
                });
                Object.keys(VIEWS).forEach(function (name) {
                    candidates.push({ kind: 'view', name: name });
                });
                const nodes = candidates.filter(function (item) {
                    return item.name.toLowerCase().indexOf(keyword) >= 0 &&
                        (!kinds.length || kinds.indexOf(item.kind) >= 0);
                }).slice(0, limit).map(function (item) {
                    return node(item.kind, item.name, 'main', true, item.kind);
                });
                reply(reqId, { nodes: nodes, total: nodes.length, epoch: 0 });
                return { ok: true, reqId };
            }

            // ---------------------------------------------------- 表数据（M5）
            case 'table.rows': {
                try {
                    const entry = tableEntry(a.ref || {});
                    const info = editableInfo(entry, a.sessionId);
                    const identity = tableIdentity(entry);
                    const pageSize = Math.max(1, Math.min(2000, Number(a.pageSize) || 500));
                    const page = Math.max(0, Number(a.page) || 0);
                    const entries = tableEntries(entry, a);
                    const start = page * pageSize;
                    const slice = entries.slice(start, start + pageSize);
                    reply(reqId, {
                        columns: entry.def.columns.map(function (column) {
                            return {
                                name: column.name,
                                type: column.type,
                                kind: column.kind,
                                nullable: column.nullable !== false,
                                primaryKey: !!column.primaryKey,
                                autoIncrement: false,
                                default: null,
                                comment: ''
                            };
                        }),
                        identity: identity,
                        editable: info.editable,
                        readOnlyReason: info.reason,
                        rows: slice.map(function (item) { return clone(item.values); }),
                        rowKeys: slice.map(function (item) {
                            return rowKeysFor(entry, identity, item);
                        }),
                        page: page,
                        pageSize: pageSize,
                        hasMore: entries.length > start + pageSize,
                        totalEstimate: entries.length,
                        elapsedMs: 6,
                        warnings: []
                    });
                } catch (error) {
                    fail(reqId, error);
                }
                return { ok: true, reqId };
            }

            case 'table.count': {
                try {
                    const entry = tableEntry(a.ref || {});
                    const entries = tableEntries(entry, a);
                    reply(reqId, { count: entries.length, elapsedMs: 5 });
                } catch (error) {
                    fail(reqId, error);
                }
                return { ok: true, reqId };
            }

            case 'table.apply': {
                try {
                    const entry = tableEntry(a.ref || {});
                    const info = editableInfo(entry, a.sessionId);
                    if (!info.editable) throw { code: 'read_only', message: info.reason };
                    const identity = tableIdentity(entry);
                    const changes = a.changes || {};
                    if (a.dryRun) {
                        reply(reqId, {
                            dryRun: true,
                            sqlPreview: tableSqlPreview(entry, identity, changes),
                            statements: [],
                            affected: 0
                        });
                    } else {
                        const result = applyTableChanges(entry, identity, changes);
                        reply(reqId, {
                            dryRun: false,
                            sqlPreview: result.preview,
                            statements: [],
                            affected: result.affected,
                            applied: (changes.inserts || []).length +
                                (changes.updates || []).length +
                                (changes.deletes || []).length,
                            elapsedMs: 8,
                            inTransaction: false
                        });
                    }
                } catch (error) {
                    fail(reqId, error);
                }
                return { ok: true, reqId };
            }

            case 'table.cell': {
                try {
                    const entry = tableEntry(a.ref || {});
                    const identity = tableIdentity(entry);
                    const columnIndex = entry.def.columns.findIndex(function (column) {
                        return column.name === a.column;
                    });
                    if (columnIndex < 0) {
                        throw { code: 'bad_args', message: '列不存在: ' + a.column };
                    }
                    const item = tableEntries(entry, {}).find(function (candidate) {
                        return rowMatchesKeys(entry, identity, candidate, a.keys || {});
                    });
                    if (!item) throw { code: 'not_found', message: '该行已不存在或已被修改' };
                    const column = entry.def.columns[columnIndex];
                    const value = item.values[columnIndex];
                    reply(reqId, {
                        kind: column.kind || 'text',
                        text: value == null ? '' : String(value),
                        isNull: value == null,
                        bytes: value == null ? 0 : String(value).length,
                        truncated: false,
                        columnType: column.type
                    });
                } catch (error) {
                    fail(reqId, error);
                }
                return { ok: true, reqId };
            }

            case 'table.truncate': {
                try {
                    const entry = tableEntry(a.ref || {});
                    const info = editableInfo(entry, a.sessionId);
                    if (!info.editable) throw { code: 'read_only', message: info.reason };
                    if (a.confirmName !== entry.name) {
                        throw { code: 'bad_args', message: '表名不匹配' };
                    }
                    ensureRowIds(entry);
                    const affected = entry.def.rows.length;
                    entry.def.rows.length = 0;
                    entry.def._rowids.length = 0;
                    reply(reqId, { affected: affected, elapsedMs: 5 });
                } catch (error) {
                    fail(reqId, error);
                }
                return { ok: true, reqId };
            }

            // ---------------------------------------------------- 查询
            case 'query.execute':
                startQuery(reqId, a);
                return { ok: true, reqId };

            case 'query.cancel':
                reply(reqId, { ok: true });
                return { ok: true, reqId };

            case 'query.explain': {
                const engine = sessionEngine(a.sessionId);
                let plan = [['2', '0', '0', 'SCAN users'], ['8', '0', '0', 'USE TEMP B-TREE FOR ORDER BY']];
                let warnings = [];
                if (engine === 'mysql') {
                    plan = [['id', 'select_type', 'table', 'type', 'key', 'rows', 'Extra'],
                        ['1', 'SIMPLE', 'users', 'ALL', null, '128', 'Using temporary; Using filesort']];
                    if (a.analyze) warnings.push('EXPLAIN ANALYZE 需要 MySQL 8.0.18+，旧版本可能报错');
                } else if (engine === 'postgres') {
                    plan = [['Seq Scan on users  (cost=0.00..14.50 rows=128 width=64)',
                        '  Filter: (created_at > now() - interval \'7 days\')']];
                    if (a.analyze) warnings.push('EXPLAIN ANALYZE 会真实执行语句，写语句会修改数据');
                }
                reply(reqId, {
                    format: 'text', engine: engine,
                    plan: plan, warnings: warnings
                });
                return { ok: true, reqId };
            }

            case 'app.read_file': {
                const path = String(a.path || '');
                if (!/\.sql$/i.test(path)) {
                    fail(reqId, { code: 'bad_args', message: 'Mock 仅支持 .sql 文件' });
                    return { ok: true, reqId };
                }
                const mockFile = mockFiles.get(path);
                if (!mockFile) {
                    fail(reqId, { code: 'io_error', message: '文件不存在' });
                    return { ok: true, reqId };
                }
                reply(reqId, { ok: true, path: path, content: mockFile, bytes: mockFile.length });
                return { ok: true, reqId };
            }

            case 'app.save_file': {
                const path = String(a.path || '');
                const content = String(a.content || '');
                mockFiles.set(path, content);
                reply(reqId, { ok: true, path: path, bytes: content.length });
                return { ok: true, reqId };
            }

            default:
                return { ok: false, error: { code: 'unknown_command', message: '未知命令: ' + cmd } };
        }
    }

    async function invoke(cmd, args) {
        if (cmd === 'ipc.call') return startTask(args || {});
        if (cmd === 'ipc.cancel') {
            const reqId = (args || {}).reqId;
            const task = tasks.get(reqId);
            if (!task) return { ok: true, found: false };
            task.cancelled = true;
            if (task.timer) clearTimeout(task.timer);
            tasks.delete(reqId);
            later(function () { emit('ipc.event', { reqId: reqId, type: 'cancelled' }); });
            return { ok: true, found: true };
        }
        return { ok: false, error: { code: 'unknown_command', message: '未知命令: ' + cmd } };
    }

    window.MockBridge = { invoke: invoke, listen: listen, emit: emit, isMock: true };
})();
