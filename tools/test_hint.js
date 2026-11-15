#!/usr/bin/env node
// 前端智能补全的无浏览器单元自测：node tools/test_hint.js
//
// 用 vm 加载 frontend/js/editor/hint.js，替身实现 meta.children，覆盖：
//   关键字 + 表名、表名.列名、别名.列名、FROM 表的裸列名、按库缓存隔离、invalidate。
'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const root = path.resolve(__dirname, '..');
const source = fs.readFileSync(path.join(root, 'frontend/js/editor/hint.js'), 'utf8');

const DATA = {
    employees: {
        employees: ['emp_no', 'first_name', 'last_name', 'hire_date'],
        salaries: ['emp_no', 'salary', 'from_date', 'to_date'],
        departments: ['dept_no', 'dept_name'],
        views: ['dept_emp_latest_date']
    },
    sakila: {
        actor: ['actor_id', 'first_name', 'last_name'],
        film: ['film_id', 'title', 'release_year'],
        views: ['actor_info']
    }
};

const calls = [];

async function metaChildren(sessionId, parent) {
    calls.push({ sessionId: sessionId, parent: Object.assign({}, parent) });
    if (parent.kind === 'connection' || parent.name === 'databases') {
        return { nodes: ['employees', 'sakila'].map(function (name) {
            return { kind: 'database', name: name };
        }) };
    }
    const db = DATA[parent.database] || DATA.employees;
    if (parent.name === 'tables') {
        return {
            nodes: Object.keys(db).filter(function (key) { return key !== 'views'; })
                .map(function (name) { return { kind: 'table', name: name }; })
        };
    }
    if (parent.name === 'views') {
        return {
            nodes: (db.views || []).map(function (name) {
                return { kind: 'view', name: name };
            })
        };
    }
    if (parent.name === 'columns') {
        const cols = db[parent.table] || [];
        return {
            nodes: cols.map(function (name, index) {
                return { kind: 'column', name: name, badge: index === 0 ? 'int · PK' : 'text' };
            })
        };
    }
    return { nodes: [] };
}

const sandbox = {
    window: {},
    console: { warn: function () {}, log: function () {} },
    Promise: Promise, Map: Map, Set: Set, Array: Array, Object: Object,
    String: String, Number: Number, RegExp: RegExp, Error: Error, JSON: JSON
};
vm.createContext(sandbox);
vm.runInContext(source, sandbox, { filename: 'hint.js' });

const hint = sandbox.window.SilkitHint.create({
    api: { metaChildren: metaChildren },
    state: {},
    toast: function () {}
});

const session = { id: 's1', profile: { engine: 'mysql' }, currentDb: 'employees' };

function makeEditor(text, caret) {
    const before = text.slice(0, caret == null ? text.length : caret);
    const lines = before.split('\n');
    const cursor = { line: lines.length - 1, ch: lines[lines.length - 1].length };
    const allLines = text.split('\n');
    return {
        getCursor: function () { return { line: cursor.line, ch: cursor.ch }; },
        getLine: function (n) { return allLines[n] || ''; },
        getRange: function () { return before; },
        getValue: function () { return text; }
    };
}

function texts(result) {
    return result.list.map(function (item) { return item.text; });
}

async function complete(sql, caret, context) {
    const ctx = Object.assign({ database: 'employees', schema: '', table: '' }, context || {});
    return hint.hint(session, function () { return ctx; })(makeEditor(sql, caret));
}

let passed = 0;
function check(name, condition) {
    assert.ok(condition, name);
    passed += 1;
    console.log('  [PASS] ' + name);
}

(async function () {
    // 1. 裸标识符：关键字 + 表/视图 + FROM 表的列
    const bare = await complete('SELECT * FROM employees WHERE emp');
    check('输入表名前缀时表进入候选', texts(bare).indexOf('employees') >= 0);
    check('以 emp 开头的关键字/表/列都被过滤',
        bare.list.every(function (item) { return item.text.toLowerCase().indexOf('emp') === 0; }));
    check('候选带类型 class', bare.list.some(function (item) {
        return item.className === 'CodeMirror-hint-table';
    }));

    const any = await complete('SELECT * FROM ');
    check('空前缀时视图名进入候选', texts(any).indexOf('dept_emp_latest_date') >= 0);
    check('空前缀时关键字进入候选', texts(any).indexOf('SELECT') >= 0);
    check('视图标记为 view', any.list.some(function (item) {
        return item.text === 'dept_emp_latest_date' &&
            item.displayText.indexOf('(view)') >= 0;
    }));

    const keyword = await complete('SEL');
    check('关键字前缀过滤', texts(keyword).indexOf('SELECT') >= 0 &&
        texts(keyword).indexOf('FROM') < 0);

    // 2. 表名. 只提示该表的列，插入点位于点号之后
    const dotted = await complete('SELECT employees.');
    check('表名. 后只提示列', dotted.list.length > 0 && dotted.list.every(function (item) {
        return item.className === 'CodeMirror-hint-column';
    }));
    check('表名. 提示包含该表列', texts(dotted).indexOf('first_name') >= 0);
    check('表名. 不混入其他表列', texts(dotted).indexOf('salary') < 0);
    check('插入位置在点号之后', dotted.from.ch === 'SELECT employees.'.length &&
        dotted.to.ch === dotted.from.ch);
    check('列显示类型说明', dotted.list[0].displayText.indexOf('int') >= 0);

    // 3. 输入列名前缀时过滤
    const partial = await complete('SELECT employees.emp_');
    check('点号后的列名前缀过滤', texts(partial).indexOf('emp_no') >= 0 &&
        texts(partial).indexOf('first_name') < 0);
    check('部分输入时 from 指向列名起点', partial.from.ch === 'SELECT employees.'.length);

    // 4. 别名. 解析到真实表
    const alias = await complete('SELECT * FROM employees e WHERE e.');
    check('别名. 解析到真实表的列', texts(alias).indexOf('hire_date') >= 0);
    check('别名. 不带入关键字', alias.list.every(function (item) {
        return item.className === 'CodeMirror-hint-column';
    }));

    // 5. FROM 中出现的表，其列也进入裸标识符提示
    const columns = await complete('SELECT * FROM employees WHERE hire');
    check('FROM 表的列进入裸标识符提示', texts(columns).indexOf('hire_date') >= 0);

    // 6. 上下文里选中的表（对象树）也参与提示
    const picked = await complete('SELECT last_', null, { table: 'employees' });
    check('树中选中的表也提供列', texts(picked).indexOf('last_name') >= 0);

    // 7. 按库隔离缓存：切到 sakila 后不再出现 employees 的表
    calls.length = 0;
    const other = await complete('SELECT * FROM ', null, { database: 'sakila', schema: '' });
    check('切换到其他库后只列出该库的表', texts(other).indexOf('actor') >= 0 &&
        texts(other).indexOf('employees') < 0);
    check('元数据请求带上了目标库', calls.some(function (item) {
        return item.parent.name === 'tables' && item.parent.database === 'sakila';
    }));

    // 8. invalidate 后重新读取元数据
    calls.length = 0;
    hint.invalidate(session.id);
    await complete('SELECT * FROM ', null, { database: 'sakila' });
    check('invalidate 会重新拉取元数据', calls.some(function (item) {
        return item.parent.name === 'tables' && item.parent.database === 'sakila';
    }));

    // 9. PG schema 透传
    calls.length = 0;
    await complete('SELECT * FROM public.employees.', null,
        { database: 'employees', schema: 'analytics' });
    check('列请求透传 schema', calls.some(function (item) {
        return item.parent.name === 'columns' && item.parent.schema === 'analytics';
    }));

    // 10. 点号补全严格化：库限定符、未知表不串列（2026-11-15 回归）
    hint.invalidate(session.id);
    const dbQualified = await complete('SELECT * FROM employees.');
    check('FROM 后「库名.」提示该库的表而不是某张表的列',
        texts(dbQualified).indexOf('employees') >= 0 &&
        dbQualified.list.every(function (item) {
            return item.className === 'CodeMirror-hint-table';
        }));

    const dbPartial = await complete('SELECT *\nFROM employees.b\nWHERE emp_no = 10010',
        'SELECT *\nFROM employees.b'.length);
    check('FROM 库名. 输入列名前缀时不再串出别的表的列',
        dbPartial.list.every(function (item) {
            return item.className !== 'CodeMirror-hint-column';
        }));

    const whereQualified = await complete('SELECT *\nFROM employees.birth_date\nWHERE employees.');
    check('WHERE 中「表名.」只提示该表的列', texts(whereQualified).indexOf('hire_date') >= 0 &&
        texts(whereQualified).indexOf('salary') < 0 &&
        whereQualified.list.every(function (item) {
            return item.className === 'CodeMirror-hint-column';
        }));

    const unknown = await complete('SELECT unknown_table.');
    check('未知「表名.」不回退到关键字或其他表的列', unknown.list.length === 0);

    const otherTable = await complete('SELECT salaries.');
    check('salaries. 只提示 salaries 的列', texts(otherTable).indexOf('salary') >= 0 &&
        texts(otherTable).indexOf('first_name') < 0);

    const bareNoLeak = await complete('SELECT * FROM employees.birth_date WHERE emp');
    check('裸标识符不把 db.column 中的列名当表，避免串列',
        texts(bareNoLeak).indexOf('salary') < 0 && texts(bareNoLeak).indexOf('dept_no') < 0);

    // 11. 候选顺序：常用关键字 → 表名 → 列名 → 冷门关键字
    const ordered = await complete('SELECT * FROM employees WHERE ');
    const order = texts(ordered);
    check('常用关键字排在首位', ordered.list.length > 0 && ordered.list[0]._priority === 0);
    check('顺序为 常用关键字 / 表名 / 列名 / 冷门关键字',
        order.indexOf('SELECT') < order.indexOf('employees') &&
        order.indexOf('employees') < order.indexOf('hire_date') &&
        order.indexOf('hire_date') < order.indexOf('AUTO_INCREMENT'));
    check('冷门关键字仍在候选末尾而非被丢弃',
        order.indexOf('COALESCE()') >= 0 &&
        ordered.list[ordered.list.length - 1]._priority === 3);

    console.log('\n智能补全自测通过：' + passed + ' 项');
})().catch(function (error) {
    console.error('智能补全自测失败：', error && error.message || error);
    process.exit(1);
});
