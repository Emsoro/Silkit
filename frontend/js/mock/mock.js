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
        { name: 'id', type: 'INTEGER', kind: 'int', nullable: false, primaryKey: true, autoIncrement: true },
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
                { name: 'id', type: 'INTEGER', kind: 'int', nullable: false, primaryKey: true, autoIncrement: true },
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
                { name: 'id', type: 'INTEGER', kind: 'int', nullable: false, primaryKey: true, autoIncrement: true },
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
        {
            owner: 'orders', name: 'fk_orders_user_id', id: 0, seq: 0, table: 'users',
            from: 'user_id', to: 'id', onUpdate: 'NO ACTION', onDelete: 'NO ACTION', match: 'NONE'
        }
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

    // ---------------------------------------------------------------- M6 DDL（mock）
    function mockDdlEngine(sessionId) {
        return sessionEngine(sessionId) || 'sqlite';
    }

    function mockQuote(engine, name) {
        if (engine === 'mysql') return '`' + String(name).replace(/`/g, '``') + '`';
        return '"' + String(name).replace(/"/g, '""') + '"';
    }

    function mockTableName(engine, scope, name) {
        const quoted = mockQuote(engine, name);
        if (engine === 'mysql' && scope && scope !== 'main') {
            return mockQuote(engine, scope) + '.' + quoted;
        }
        if (engine === 'postgres' && scope) {
            return mockQuote(engine, scope) + '.' + quoted;
        }
        return quoted;
    }

    function mockKind(type) {
        const text = String(type || '').toLowerCase();
        if (text.indexOf('int') >= 0) return 'int';
        if (text.indexOf('bool') >= 0) return 'bool';
        if (/(decimal|numeric|money)/.test(text)) return 'decimal';
        if (/(real|float|double)/.test(text)) return 'float';
        if (/(blob|binary|bytea)/.test(text)) return 'binary';
        if (text.indexOf('json') >= 0) return 'json';
        if (/timestamp|datetime/.test(text)) return 'datetime';
        if (/(^|\W)date(\W|$)/.test(text)) return 'date';
        if (/(^|\W)time(\W|$)/.test(text)) return 'time';
        return 'text';
    }

    function mockDdlDefinition(a) {
        const ref = a.ref || {};
        const name = ref.name;
        if (!name) throw { code: 'bad_args', message: '缺少表名' };
        if (ref.kind && ref.kind !== 'table') {
            throw { code: 'bad_args', message: '设计器仅支持表对象' };
        }
        const entry = tableEntry({ name: name, kind: 'table' });
        if (entry.isView) throw { code: 'bad_args', message: '设计器仅支持表对象' };
        const engine = mockDdlEngine(a.sessionId);
        return {
            engine: engine,
            scope: ref.database || ref.schema || 'main',
            name: name,
            exists: true,
            primaryKeyName: '',
            columns: entry.def.columns.map(function (column) {
                return {
                    name: column.name,
                    type: column.type,
                    notNull: column.nullable === false,
                    primaryKey: !!column.primaryKey,
                    autoIncrement: !!column.autoIncrement,
                    hasDefault: column.hasDefault === true || String(column.default || '').trim() !== '',
                    default: String(column.default || ''),
                    comment: String(column.comment || ''),
                    oldName: ''
                };
            }),
            indexes: INDEXES.filter(function (index) {
                return index.table === name;
            }).map(function (index) {
                return { name: index.name, unique: !!index.unique, columns: index.columns.slice() };
            }),
            foreignKeys: FOREIGN_KEYS.filter(function (fk) {
                return fk.owner === name;
            }).map(function (fk) {
                return {
                    name: fk.name || '',
                    columns: [fk.from],
                    refTable: fk.table,
                    refSchema: '',
                    refColumns: [fk.to],
                    onUpdate: fk.onUpdate || 'NO ACTION',
                    onDelete: fk.onDelete || 'NO ACTION'
                };
            }),
            options: { engine: '', charset: '', collation: '', comment: '' }
        };
    }

    function mockDdlNormalize(a) {
        const source = a.definition || {};
        const ref = a.ref || {};
        const scope = String(source.scope || ref.database || ref.schema || 'main');
        const columns = (source.columns || []).map(function (column) {
            return {
                name: String(column.name || '').trim(),
                type: String(column.type || '').trim(),
                notNull: !!column.notNull,
                primaryKey: !!column.primaryKey,
                autoIncrement: !!column.autoIncrement,
                hasDefault: !!column.hasDefault && String(column.default || '').trim() !== '',
                default: String(column.default || ''),
                comment: String(column.comment || ''),
                oldName: String(column.oldName || '')
            };
        }).filter(function (column) { return column.name !== ''; });
        const indexes = (source.indexes || []).map(function (index) {
            return {
                name: String(index.name || '').trim(),
                unique: !!index.unique,
                columns: (index.columns || []).filter(Boolean)
            };
        }).filter(function (index) { return index.columns.length > 0; });
        const foreignKeys = (source.foreignKeys || []).map(function (fk) {
            return {
                name: String(fk.name || '').trim(),
                columns: (fk.columns || []).filter(Boolean),
                refTable: String(fk.refTable || '').trim(),
                refColumns: (fk.refColumns || []).filter(Boolean),
                onUpdate: fk.onUpdate || 'NO ACTION',
                onDelete: fk.onDelete || 'NO ACTION'
            };
        }).filter(function (fk) { return fk.columns.length > 0 && fk.refTable !== ''; });
        return {
            scope: scope,
            name: String(source.name || a.newName || ref.name || '').trim(),
            primaryKeyName: String(source.primaryKeyName || ''),
            columns: columns,
            indexes: indexes,
            foreignKeys: foreignKeys,
            options: source.options || {}
        };
    }

    function mockAssignNames(items, prefix, table) {
        const used = {};
        return items.map(function (item) {
            const base = String(item.name || '').trim() ||
                (prefix + '_' + table + '_' + item.columns.map(function (column) {
                    return String(column).replace(/[^A-Za-z0-9_]/g, '_');
                }).join('_'));
            let candidate = base;
            let suffix = 2;
            while (used[candidate]) {
                candidate = base + '_' + suffix;
                suffix += 1;
            }
            used[candidate] = true;
            const copy = {};
            Object.keys(item).forEach(function (key) { copy[key] = item[key]; });
            copy.resolvedName = candidate;
            return copy;
        });
    }

    function mockColumnSql(engine, column) {
        const name = mockQuote(engine, column.name);
        const type = column.type || 'TEXT';
        if (engine === 'sqlite' && column.primaryKey && column.autoIncrement) {
            return name + ' INTEGER PRIMARY KEY AUTOINCREMENT';
        }
        if (engine === 'postgres' && column.autoIncrement) {
            return name + ' ' + type + ' GENERATED BY DEFAULT AS IDENTITY' +
                (column.notNull ? ' NOT NULL' : '');
        }
        const parts = [name, type];
        if (engine === 'mysql' && column.autoIncrement) parts.push('AUTO_INCREMENT');
        if (column.notNull && !column.primaryKey) parts.push('NOT NULL');
        if (column.hasDefault && String(column.default || '').trim() !== '') {
            parts.push('DEFAULT ' + String(column.default).trim());
        }
        return parts.join(' ');
    }

    function mockForeignKeySql(engine, fk) {
        const columns = fk.columns.map(function (name) {
            return mockQuote(engine, name);
        }).join(', ');
        const refColumns = fk.refColumns.map(function (name) {
            return mockQuote(engine, name);
        }).join(', ');
        let sql = 'FOREIGN KEY (' + columns + ') REFERENCES ' + mockQuote(engine, fk.refTable) +
            ' (' + refColumns + ')';
        if (fk.onUpdate && fk.onUpdate !== 'NO ACTION') sql += ' ON UPDATE ' + fk.onUpdate;
        if (fk.onDelete && fk.onDelete !== 'NO ACTION') sql += ' ON DELETE ' + fk.onDelete;
        return sql;
    }

    function mockCreateSql(engine, scope, definition) {
        const lines = definition.columns.map(function (column) {
            return '  ' + mockColumnSql(engine, column);
        });
        if (engine !== 'sqlite') {
            const primary = definition.columns.filter(function (column) { return column.primaryKey; });
            if (primary.length) {
                lines.push('  PRIMARY KEY (' + primary.map(function (column) {
                    return mockQuote(engine, column.name);
                }).join(', ') + ')');
            }
        }
        definition.foreignKeys.forEach(function (fk) {
            const constraint = fk.resolvedName
                ? 'CONSTRAINT ' + mockQuote(engine, fk.resolvedName) + ' ' : '';
            lines.push('  ' + constraint + mockForeignKeySql(engine, fk));
        });
        let sql = 'CREATE TABLE ' + mockTableName(engine, scope, definition.name) +
            ' (\n' + lines.join(',\n') + '\n)';
        if (engine === 'mysql') {
            const options = definition.options || {};
            sql += ' ENGINE=' + (options.engine || 'InnoDB');
            sql += ' DEFAULT CHARSET=' + (options.charset || 'utf8mb4');
            if (options.collation) sql += ' COLLATE=' + options.collation;
            if (options.comment) {
                sql += " COMMENT='" + String(options.comment).replace(/'/g, "''") + "'";
            }
        }
        return sql + ';';
    }

    function mockCreateIndexSql(engine, scope, table, index) {
        return 'CREATE ' + (index.unique ? 'UNIQUE ' : '') + 'INDEX ' +
            mockQuote(engine, index.resolvedName) + ' ON ' + mockTableName(engine, scope, table) +
            ' (' + index.columns.map(function (name) {
                return mockQuote(engine, name);
            }).join(', ') + ');';
    }

    function mockDropIndexSql(engine, scope, table, index) {
        if (engine === 'mysql') {
            return 'DROP INDEX ' + mockQuote(engine, index.resolvedName) + ' ON ' +
                mockTableName(engine, scope, table) + ';';
        }
        const prefix = engine === 'postgres' && scope ? mockQuote(engine, scope) + '.' : '';
        return 'DROP INDEX ' + prefix + mockQuote(engine, index.resolvedName) + ';';
    }

    function mockAddForeignKeySql(engine, scope, table, fk) {
        const constraint = fk.resolvedName
            ? 'CONSTRAINT ' + mockQuote(engine, fk.resolvedName) + ' ' : '';
        return 'ALTER TABLE ' + mockTableName(engine, scope, table) + ' ADD ' + constraint +
            mockForeignKeySql(engine, fk) + ';';
    }

    function mockModifyColumnSql(engine, scope, table, oldName, column) {
        const target = mockTableName(engine, scope, table);
        if (engine === 'mysql') {
            return ['ALTER TABLE ' + target + ' CHANGE COLUMN ' + mockQuote(engine, oldName) + ' ' +
                mockColumnSql(engine, column) + ';'];
        }
        const name = mockQuote(engine, column.name);
        const statements = [
            'ALTER TABLE ' + target + ' ALTER COLUMN ' + name + ' TYPE ' + (column.type || 'text') + ';',
            'ALTER TABLE ' + target + ' ALTER COLUMN ' + name +
                (column.notNull ? ' SET NOT NULL;' : ' DROP NOT NULL;')
        ];
        if (column.hasDefault && String(column.default || '').trim() !== '') {
            statements.push('ALTER TABLE ' + target + ' ALTER COLUMN ' + name +
                ' SET DEFAULT ' + String(column.default).trim() + ';');
        } else {
            statements.push('ALTER TABLE ' + target + ' ALTER COLUMN ' + name + ' DROP DEFAULT;');
        }
        if (column.comment) {
            statements.push('COMMENT ON COLUMN ' + target + '.' + name + " IS '" +
                String(column.comment).replace(/'/g, "''") + "';");
        }
        return statements;
    }

    function mockSameFk(left, right) {
        return left.columns.join(',') === right.columns.join(',') &&
            left.refTable === right.refTable &&
            left.refColumns.join(',') === right.refColumns.join(',') &&
            (left.onUpdate || 'NO ACTION') === (right.onUpdate || 'NO ACTION') &&
            (left.onDelete || 'NO ACTION') === (right.onDelete || 'NO ACTION');
    }

    function mockAlterStatements(engine, scope, original, updated, newName) {
        const statements = [];
        const warnings = [];
        let table = original.name;
        const target = newName && newName !== original.name ? newName : updated.name;

        if (engine === 'sqlite') {
            const originalByName = {};
            original.columns.forEach(function (column) { originalByName[column.name] = column; });
            updated.columns.forEach(function (column) {
                const previous = originalByName[column.oldName || column.name];
                if (!previous) return;
                if (column.type !== previous.type || column.notNull !== previous.notNull ||
                    column.hasDefault !== previous.hasDefault ||
                    String(column.default) !== String(previous.default)) {
                    throw {
                        code: 'bad_args',
                        message: 'SQLite 不支持修改列定义（' + column.name + '），请重建表'
                    };
                }
            });
        }

        if (target && target !== original.name) {
            statements.push('ALTER TABLE ' + mockTableName(engine, scope, table) + ' RENAME TO ' +
                mockQuote(engine, target) + ';');
            table = target;
        }

        const originalIndexes = mockAssignNames(original.indexes, 'idx', original.name);
        const updatedIndexes = mockAssignNames(updated.indexes, 'idx', table);
        const updatedIndexMap = {};
        updatedIndexes.forEach(function (index) { updatedIndexMap[index.resolvedName] = index; });
        originalIndexes.forEach(function (index) {
            const next = updatedIndexMap[index.resolvedName];
            if (!next || next.unique !== index.unique ||
                next.columns.join(',') !== index.columns.join(',')) {
                statements.push(mockDropIndexSql(engine, scope, table, index));
            }
        });

        const originalFks = mockAssignNames(original.foreignKeys, 'fk', original.name);
        const updatedFks = mockAssignNames(updated.foreignKeys, 'fk', table);
        const originalFkMap = {};
        const updatedFkMap = {};
        originalFks.forEach(function (fk) { originalFkMap[fk.resolvedName] = fk; });
        updatedFks.forEach(function (fk) { updatedFkMap[fk.resolvedName] = fk; });
        const dropFk = engine === 'mysql' ? ' DROP FOREIGN KEY ' : ' DROP CONSTRAINT ';
        if (engine !== 'sqlite') {
            originalFks.forEach(function (fk) {
                const next = updatedFkMap[fk.resolvedName];
                if (!next || !mockSameFk(fk, next)) {
                    statements.push('ALTER TABLE ' + mockTableName(engine, scope, table) + dropFk +
                        mockQuote(engine, fk.resolvedName) + ';');
                }
            });
        }

        const originalByName = {};
        original.columns.forEach(function (column) { originalByName[column.name] = column; });
        const kept = {};
        updated.columns.forEach(function (column) {
            const oldName = column.oldName && originalByName[column.oldName]
                ? column.oldName : column.name;
            const previous = originalByName[oldName];
            if (!previous) {
                statements.push('ALTER TABLE ' + mockTableName(engine, scope, table) + ' ADD COLUMN ' +
                    mockColumnSql(engine, column) + ';');
                return;
            }
            kept[oldName] = true;
            if (oldName !== column.name) {
                statements.push('ALTER TABLE ' + mockTableName(engine, scope, table) +
                    ' RENAME COLUMN ' + mockQuote(engine, oldName) + ' TO ' +
                    mockQuote(engine, column.name) + ';');
            }
            const changed = column.type !== previous.type || column.notNull !== previous.notNull ||
                column.hasDefault !== previous.hasDefault ||
                String(column.default) !== String(previous.default) ||
                String(column.comment || '') !== String(previous.comment || '');
            if (changed && engine !== 'sqlite') {
                mockModifyColumnSql(engine, scope, table, column.name, column)
                    .forEach(function (sql) { statements.push(sql); });
            }
        });

        original.columns.forEach(function (column) {
            if (kept[column.name]) return;
            const renamed = updated.columns.some(function (item) {
                return item.oldName === column.name;
            });
            if (renamed) return;
            statements.push('ALTER TABLE ' + mockTableName(engine, scope, table) + ' DROP COLUMN ' +
                mockQuote(engine, column.name) + ';');
        });

        const originalIndexMap = {};
        originalIndexes.forEach(function (index) { originalIndexMap[index.resolvedName] = index; });
        updatedIndexes.forEach(function (index) {
            const previous = originalIndexMap[index.resolvedName];
            if (!previous || previous.unique !== index.unique ||
                previous.columns.join(',') !== index.columns.join(',')) {
                statements.push(mockCreateIndexSql(engine, scope, table, index));
            }
        });

        updatedFks.forEach(function (fk) {
            const previous = originalFkMap[fk.resolvedName];
            if (previous && mockSameFk(fk, previous)) return;
            if (engine === 'sqlite') {
                warnings.push('SQLite 不支持在线添加外键，请重建表');
                return;
            }
            statements.push(mockAddForeignKeySql(engine, scope, table, fk));
        });

        return {
            statements: statements,
            warnings: warnings,
            table: table,
            target: target || original.name,
            indexes: updatedIndexes,
            foreignKeys: updatedFks
        };
    }

    function mockDdlToken(statements) {
        let hash = 1469598103934665603n;
        const text = statements.join('\n');
        for (let i = 0; i < text.length; i += 1) {
            hash ^= BigInt(text.charCodeAt(i));
            hash = (hash * 1099511628211n) & 0xFFFFFFFFFFFFFFFFn;
        }
        return hash.toString(16);
    }

    function mockDdlPlan(a) {
        const engine = mockDdlEngine(a.sessionId);
        const mode = String(a.mode || 'create');
        const definition = mockDdlNormalize(a);
        const ref = a.ref || {};
        const scope = definition.scope || ref.database || ref.schema || 'main';
        const newName = String(a.newName || '').trim();
        const notSqlite = engine !== 'sqlite';
        let statements = [];
        let warnings = [];
        let next = null;

        if (mode === 'database.create') {
            if (!notSqlite) throw { code: 'bad_args', message: 'SQLite 不支持新建数据库' };
            const name = newName || definition.name;
            if (!name) throw { code: 'bad_args', message: '数据库名不能为空' };
            statements = [engine === 'mysql'
                ? 'CREATE DATABASE ' + mockQuote(engine, name) + ' DEFAULT CHARACTER SET utf8mb4;'
                : 'CREATE DATABASE ' + mockQuote(engine, name) + ';'];
            next = { database: name };
        } else if (mode === 'database.drop') {
            if (!notSqlite) throw { code: 'bad_args', message: 'SQLite 不支持删除数据库' };
            const name = newName || definition.name || ref.database || '';
            if (!name) throw { code: 'bad_args', message: '数据库名不能为空' };
            statements = ['DROP DATABASE ' + mockQuote(engine, name) + ';'];
            next = { database: name };
        } else if (mode === 'drop' || mode === 'truncate') {
            const name = definition.name || ref.name;
            if (!name) throw { code: 'bad_args', message: '表名不能为空' };
            const target = mockTableName(engine, scope, name);
            if (mode === 'drop') {
                statements = ['DROP TABLE ' + target + ';'];
                next = { dropName: name, scope: scope };
            } else {
                statements = [engine === 'mysql'
                    ? 'TRUNCATE TABLE ' + target + ';'
                    : 'DELETE FROM ' + target + ';'];
                next = { truncateName: name, scope: scope };
            }
        } else if (mode === 'create') {
            if (!definition.name) throw { code: 'bad_args', message: '表名不能为空' };
            if (!definition.columns.length) throw { code: 'bad_args', message: '至少需要一列' };
            if (TABLES[definition.name] || VIEWS[definition.name]) {
                throw { code: 'bad_args', message: '对象已存在: ' + definition.name };
            }
            const indexes = mockAssignNames(definition.indexes, 'idx', definition.name);
            const foreignKeys = mockAssignNames(definition.foreignKeys, 'fk', definition.name);
            const draft = {
                scope: scope,
                name: definition.name,
                columns: definition.columns,
                indexes: indexes,
                foreignKeys: foreignKeys,
                options: definition.options
            };
            const createSql = mockCreateSql(engine, scope, draft);
            statements.push(createSql);
            indexes.forEach(function (index) {
                statements.push(mockCreateIndexSql(engine, scope, definition.name, index));
            });
            next = {
                createName: definition.name,
                scope: scope,
                columns: definition.columns,
                indexes: indexes,
                foreignKeys: foreignKeys,
                sql: createSql
            };
        } else if (mode === 'alter') {
            const originalName = String(ref.name || definition.name || '').trim();
            if (!originalName) throw { code: 'bad_args', message: '缺少表名' };
            const original = mockDdlDefinition({
                sessionId: a.sessionId,
                ref: { kind: 'table', name: originalName, database: scope, schema: scope }
            });
            original.scope = scope;
            const result = mockAlterStatements(engine, scope, original, definition, newName);
            statements = result.statements;
            warnings = result.warnings;
            next = {
                originalName: originalName,
                name: result.table,
                scope: scope,
                columns: definition.columns,
                indexes: result.indexes,
                foreignKeys: result.foreignKeys,
                sql: mockCreateSql(engine, scope, {
                    scope: scope,
                    name: result.table,
                    columns: definition.columns,
                    indexes: result.indexes,
                    foreignKeys: result.foreignKeys,
                    options: definition.options
                })
            };
        } else {
            throw { code: 'bad_args', message: '不支持的 DDL 模式: ' + mode };
        }

        return {
            engine: engine,
            mode: mode,
            scope: scope,
            statements: statements,
            warnings: warnings,
            token: mockDdlToken(statements),
            next: next
        };
    }

    function mockDdlColumns(columns) {
        return columns.map(function (column) {
            return {
                name: column.name,
                type: column.type,
                kind: mockKind(column.type),
                nullable: !column.notNull && !column.primaryKey,
                primaryKey: !!column.primaryKey,
                autoIncrement: !!column.autoIncrement
            };
        });
    }

    function mockDdlStoreForeignKeys(owner, foreignKeys) {
        for (let i = FOREIGN_KEYS.length - 1; i >= 0; i -= 1) {
            if (FOREIGN_KEYS[i].owner === owner) FOREIGN_KEYS.splice(i, 1);
        }
        foreignKeys.forEach(function (fk) {
            FOREIGN_KEYS.push({
                owner: owner,
                name: fk.resolvedName,
                table: fk.refTable,
                from: fk.columns[0],
                to: fk.refColumns[0],
                onUpdate: fk.onUpdate || 'NO ACTION',
                onDelete: fk.onDelete || 'NO ACTION',
                match: 'NONE'
            });
        });
    }

    function mockDdlStoreIndexes(owner, indexes) {
        for (let i = INDEXES.length - 1; i >= 0; i -= 1) {
            if (INDEXES[i].table === owner) INDEXES.splice(i, 1);
        }
        indexes.forEach(function (index) {
            INDEXES.push({
                name: index.resolvedName,
                table: owner,
                unique: !!index.unique,
                columns: index.columns.slice()
            });
        });
    }

    function mockDdlApply(a, plan) {
        const mode = String(a.mode || 'create');
        if (mode === 'database.create' || mode === 'database.drop') return;
        const next = plan.next || {};

        if (mode === 'create') {
            const name = next.createName;
            TABLES[name] = {
                columns: mockDdlColumns(next.columns || []),
                rows: [],
                sql: next.sql
            };
            mockDdlStoreIndexes(name, next.indexes || []);
            mockDdlStoreForeignKeys(name, next.foreignKeys || []);
            return;
        }

        if (mode === 'drop') {
            const name = next.dropName;
            delete TABLES[name];
            for (let i = INDEXES.length - 1; i >= 0; i -= 1) {
                if (INDEXES[i].table === name) INDEXES.splice(i, 1);
            }
            for (let i = FOREIGN_KEYS.length - 1; i >= 0; i -= 1) {
                if (FOREIGN_KEYS[i].owner === name) FOREIGN_KEYS.splice(i, 1);
            }
            for (let i = TRIGGERS.length - 1; i >= 0; i -= 1) {
                if (TRIGGERS[i].table === name) TRIGGERS.splice(i, 1);
            }
            return;
        }

        if (mode === 'truncate') {
            const entry = TABLES[next.truncateName];
            if (entry) {
                entry.rows = [];
                entry._rowids = [];
            }
            return;
        }

        if (mode === 'alter') {
            const entry = TABLES[next.originalName];
            if (!entry) throw { code: 'not_found', message: '表不存在: ' + next.originalName };
            const columns = mockDdlColumns(next.columns || []);
            const sameShape = entry.columns.map(function (column) { return column.name; }).join(',') ===
                columns.map(function (column) { return column.name; }).join(',');
            const rows = sameShape ? entry.rows : [];
            const rowids = sameShape ? entry._rowids : [];

            if (next.name !== next.originalName) {
                TABLES[next.name] = entry;
                delete TABLES[next.originalName];
                INDEXES.forEach(function (index) {
                    if (index.table === next.originalName) index.table = next.name;
                });
                FOREIGN_KEYS.forEach(function (fk) {
                    if (fk.owner === next.originalName) fk.owner = next.name;
                    if (fk.table === next.originalName) fk.table = next.name;
                });
                TRIGGERS.forEach(function (trigger) {
                    if (trigger.table === next.originalName) trigger.table = next.name;
                });
            }

            entry.columns = columns;
            entry.rows = rows;
            entry._rowids = rowids;
            entry.sql = next.sql;
            mockDdlStoreIndexes(next.name, next.indexes || []);
            mockDdlStoreForeignKeys(next.name, next.foreignKeys || []);
            return;
        }

        throw { code: 'bad_args', message: '不支持的 DDL 模式: ' + mode };
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
                if (a.kind === 'csv') {
                    reply(reqId, { ok: true, path: a.save ? 'C:\\data\\export.csv' : 'C:\\data\\sample.csv' });
                    return { ok: true, reqId };
                }
                if (a.kind === 'sql') {
                    reply(reqId, { ok: true, path: a.save ? 'C:\\data\\dump.sql' : 'C:\\mock\\sample.sql' });
                    return { ok: true, reqId };
                }
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

            // ---------------------------------------------------- DDL 设计器（M6，内存模拟）
            case 'ddl.types': {
                const engine = a.engine || sessionEngine(a.sessionId) || 'sqlite';
                const types = engine === 'mysql'
                    ? ['INT', 'BIGINT', 'VARCHAR(64)', 'VARCHAR(255)', 'TEXT', 'DECIMAL(10,2)', 'DATETIME', 'JSON', 'BLOB']
                    : (engine === 'postgres'
                        ? ['integer', 'bigint', 'character varying(64)', 'text', 'numeric(10,2)', 'boolean', 'timestamp with time zone', 'jsonb', 'uuid']
                        : ['INTEGER', 'TEXT', 'REAL', 'BLOB', 'NUMERIC', 'BOOLEAN', 'DATE', 'DATETIME']);
                reply(reqId, {
                    engine: engine,
                    types: types,
                    supports: {
                        columnComment: engine !== 'sqlite',
                        tableComment: engine !== 'sqlite',
                        storageOptions: engine === 'mysql',
                        alterColumn: engine !== 'sqlite',
                        alterForeignKey: engine !== 'sqlite',
                        alterPrimaryKey: engine !== 'sqlite',
                        transactionalDdl: engine !== 'mysql',
                        database: engine !== 'sqlite'
                    }
                });
                return { ok: true, reqId };
            }

            case 'ddl.load': {
                try {
                    const definition = mockDdlDefinition(a);
                    reply(reqId, { definition: definition, warnings: [] });
                } catch (error) {
                    fail(reqId, error);
                }
                return { ok: true, reqId };
            }

            case 'ddl.tables': {
                reply(reqId, { tables: Object.keys(TABLES) });
                return { ok: true, reqId };
            }

            case 'ddl.preview': {
                try {
                    const plan = mockDdlPlan(a);
                    const engine = sessionEngine(a.sessionId);
                    if (engine === 'mysql') {
                        plan.warnings.push('MySQL DDL 会隐式提交，失败后无法自动回滚');
                    }
                    reply(reqId, {
                        engine: sessionEngine(a.sessionId),
                        mode: a.mode,
                        scope: plan.scope,
                        statements: plan.statements,
                        sql: plan.statements.join('\n\n'),
                        token: plan.token,
                        warnings: plan.warnings,
                        affectedRows: a.mode === 'create' ? '0' : '未知',
                        transactional: sessionEngine(a.sessionId) !== 'mysql'
                    });
                } catch (error) {
                    fail(reqId, error);
                }
                return { ok: true, reqId };
            }

            case 'ddl.apply': {
                try {
                    const plan = mockDdlPlan(a);
                    if (!a.token || a.token !== plan.token) {
                        throw { code: 'bad_args', message: 'DDL 已变化，请重新预览后再执行' };
                    }
                    const target = plan.next && (plan.next.dropName || plan.next.truncateName ||
                        plan.next.database || '');
                    if (a.mode === 'drop' || a.mode === 'truncate' || a.mode === 'database.drop') {
                        if (String(a.confirmName || '') !== target) {
                            throw {
                                code: 'bad_args',
                                message: a.mode === 'database.drop'
                                    ? '请输入完整数据库名确认删除操作'
                                    : '请输入完整表名确认操作'
                            };
                        }
                    }
                    if (sessions.get(a.sessionId) && sessions.get(a.sessionId).readOnly) {
                        throw { code: 'read_only', message: '只读连接不允许执行 DDL' };
                    }
                    mockDdlApply(a, plan);
                    reply(reqId, {
                        ok: true,
                        applied: plan.statements.length,
                        statements: plan.statements,
                        warnings: plan.warnings.concat(
                            sessionEngine(a.sessionId) === 'mysql'
                                ? ['MySQL DDL 会隐式提交，失败后无法自动回滚'] : []),
                        transactional: sessionEngine(a.sessionId) !== 'mysql',
                        rolledBack: false,
                        elapsedMs: 8
                    });
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

            // ---------------------------------------------------- 导入 / 导出（M7）
            case 'export.result': {
                const path = String(a.path || 'C:\\exports\\result.' + (a.format || 'csv'));
                progress(reqId, { rows: 400, bytes: 24576 });
                later(function () { progress(reqId, { rows: 800, bytes: 49152 }); }, 30);
                later(function () {
                    reply(reqId, {
                        rows: 1280,
                        bytes: 78600,
                        elapsedMs: 140,
                        path: path,
                        format: a.format || 'csv',
                        encoding: a.encoding || 'utf-8'
                    });
                }, 60);
                return { ok: true, reqId };
            }

            case 'import.preview': {
                const headers = ['id', 'name', 'email'];
                const rows = [];
                for (let i = 1; i <= 20; i += 1) {
                    rows.push([String(i), '用户 ' + i, 'user' + i + '@example.com']);
                }
                reply(reqId, {
                    path: String(a.path || 'C:\\data\\sample.csv'),
                    bytes: 4096,
                    encoding: a.encoding && a.encoding !== 'auto' ? a.encoding : 'utf-8',
                    delimiter: a.delimiter && a.delimiter !== 'auto' ? a.delimiter : ',',
                    header: a.header !== false,
                    headers: headers,
                    columns: headers.map(function (name, index) {
                        return { index: index, name: name };
                    }),
                    rows: rows,
                    total: 128
                });
                return { ok: true, reqId };
            }

            case 'import.csv': {
                progress(reqId, { rows: 64, total: 128 });
                later(function () { progress(reqId, { rows: 128, total: 128 }); }, 40);
                later(function () {
                    reply(reqId, {
                        rows: 128,
                        batches: 1,
                        elapsedMs: 90,
                        table: a.table || 'imported',
                        encoding: a.encoding || 'utf-8'
                    });
                }, 80);
                return { ok: true, reqId };
            }

            case 'dump.sql': {
                progress(reqId, { tableIndex: 1, total: 3, table: 'users', rows: 128 });
                later(function () { progress(reqId, { tableIndex: 2, total: 3, table: 'orders', rows: 64 }); }, 35);
                later(function () {
                    reply(reqId, {
                        tables: 3,
                        rows: 256,
                        bytes: 32768,
                        path: String(a.path || 'C:\\data\\dump.sql'),
                        encoding: a.encoding || 'utf-8'
                    });
                }, 90);
                return { ok: true, reqId };
            }

            case 'query.execute_file': {
                const statements = 6;
                progress(reqId, { index: 2, total: statements });
                event(reqId, 'statement', { index: 2, total: statements, offset: 120, sql: 'UPDATE users SET name = ...' });
                later(function () { progress(reqId, { index: statements, total: statements }); }, 40);
                later(function () {
                    reply(reqId, {
                        statements: Array.from({ length: statements }, function (_, index) {
                            return { index: index, rows: 0, affected: 1, elapsedMs: 8 };
                        }),
                        elapsedMs: 60
                    });
                }, 80);
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
