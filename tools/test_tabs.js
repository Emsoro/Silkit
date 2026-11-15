#!/usr/bin/env node
// 标签空态的无浏览器回归自测：node tools/test_tabs.js
//
// 守护 v1.0.2 的缺陷回归：启动时不要凭空建查询页、关闭最后一个标签后不要再弹一个。
// 用 vm 加载 frontend/js/views/tabs.js，替身实现最小 DOM / state / 查询上下文。
'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const root = path.resolve(__dirname, '..');
const source = fs.readFileSync(path.join(root, 'frontend/js/views/tabs.js'), 'utf8');

let passed = 0;
async function check(name, fn) {
    await fn();
    passed += 1;
    console.log('  ok  ' + name);
}

// ---------------------------------------------------------------- 最小 DOM
class El {
    constructor(tag) {
        this.tagName = tag;
        this.parent = null;
        this.children = [];
        this.hidden = false;
        this.className = '';
        this.textContent = '';
        this.innerHTML = '';
        this.value = '';
        this.disabled = false;
        this.title = '';
        this.attrs = {};
        this.listeners = {};
        const self = this;
        this.classList = {
            add: function () {},
            remove: function () {},
            toggle: function () {}
        };
        this.style = {};
        void self;
    }
    appendChild(child) {
        const old = this.children.indexOf(child);
        if (old >= 0) this.children.splice(old, 1);
        child.parent = this;
        this.children.push(child);
        return child;
    }
    insertBefore(child, before) {
        if (child.parent) child.remove();
        const at = before ? this.children.indexOf(before) : -1;
        this.children.splice(at < 0 ? this.children.length : at, 0, child);
        child.parent = this;
        return child;
    }
    remove() {
        if (!this.parent) return;
        const at = this.parent.children.indexOf(this);
        if (at >= 0) this.parent.children.splice(at, 1);
        this.parent = null;
    }
    setAttribute(key, value) { this.attrs[key] = String(value); }
    getAttribute(key) { return this.attrs[key]; }
    addEventListener(type, fn) { (this.listeners[type] || (this.listeners[type] = [])).push(fn); }
    querySelectorAll(selector) {
        if (selector === '.tab') {
            return this.children.filter(function (node) {
                return String(node.className).split(/\s+/).indexOf('tab') >= 0;
            });
        }
        return [];
    }
    querySelector() { return null; }
    closest() { return null; }
}

const ids = {
    tabbar: new El('div'),
    tabAdd: new El('button'),
    editorTitle: new El('span'),
    editorSub: new El('span'),
    runBtn: new El('button'),
    cancelBtn: new El('button'),
    editorPane: new El('section'),
    splitter: new El('div'),
    resultsPane: new El('section'),
    tablePane: new El('section'),
    workspaceEmpty: new El('section')
};
// sqlEditor / runScope 返回 null，让 createEditor 走降级分支（不再需要完整 CodeMirror）。
const document = {
    getElementById: function (id) { return ids[id] || null; },
    createElement: function (tag) { return new El(tag); }
};

// ---------------------------------------------------------------- 最小 state
function makeState() {
    const listeners = {};
    const state = {
        tabs: [],
        activeTabId: null,
        seq: { tab: 0 },
        sessions: new Map(),
        on: function (key, fn) { (listeners[key] || (listeners[key] = [])).push(fn); },
        emit: function (key, value) {
            (listeners[key] || []).forEach(function (fn) { fn(value); });
        },
        tab: function (id) {
            for (let i = 0; i < state.tabs.length; i += 1) {
                if (state.tabs[i].id === id) return state.tabs[i];
            }
            return null;
        },
        activeTab: function () { return state.activeTabId ? state.tab(state.activeTabId) : null; },
        setActiveTab: function (id) {
            if (id && !state.tab(id)) return null;
            state.activeTabId = id || null;
            state.emit('active-tab', state.activeTab());
            return state.activeTab();
        },
        addTab: function (tab) {
            const item = tab || {};
            item.id = 't' + (++state.seq.tab);
            item.kind = item.kind || 'query';
            item.sql = typeof item.sql === 'string' ? item.sql : '';
            item.messages = [];
            item.dirty = false;
            item.running = false;
            state.tabs.push(item);
            return item;
        },
        removeTab: function (id) {
            for (let i = 0; i < state.tabs.length; i += 1) {
                if (state.tabs[i].id !== id) continue;
                const removed = state.tabs.splice(i, 1)[0];
                if (state.activeTabId === id) {
                    const next = state.tabs[Math.min(i, state.tabs.length - 1)] || null;
                    state.activeTabId = next ? next.id : null;
                }
                state.emit('active-tab', state.activeTab());
                return removed;
            }
            return null;
        },
        session: function (id) { return state.sessions.get(id) || null; },
        activeSession: function () { return null; }
    };
    return state;
}

function makeContext() {
    const state = makeState();
    const syncs = [];
    const ctx = {
        state: state,
        query: { run: function () {}, cancel: function () {} },
        results: { show: function () {}, clear: function () {} },
        toast: function () {},
        tree: { selectedContext: function () { return null; } },
        hint: null,
        table: null,
        onTabChanged: function () {},
        onTabSqlChanged: function () {},
        queryContext: {
            sync: function (options) { syncs.push(options || {}); },
            contextFor: function () { return { session: null, database: '', schema: '', table: '', engine: 'sqlite' }; },
            ensureDatabase: function () { return Promise.resolve(true); },
            onTabActivated: function () {},
            onTabOpened: function () { return Promise.resolve(); },
            invalidate: function () {}
        }
    };
    return { state: state, ctx: ctx, syncs: syncs };
}

function loadTabs(ctx) {
    const sandbox = {
        console: { warn: function () {}, log: function () {} },
        setTimeout: setTimeout,
        clearTimeout: clearTimeout,
        document: document,
        window: {
            SilkitUtil: {
                qsa: function (selector, scope) { return (scope || document).querySelectorAll(selector); },
                el: function (tag, className, text) {
                    const node = document.createElement(tag);
                    node.className = className || '';
                    node.textContent = text || '';
                    return node;
                },
                confirm: function () { return Promise.resolve(true); },
                stripExtension: function (name) { return name; }
            },
            CodeMirror: { Doc: function (text, mode) { return { text: text, mode: mode }; } }
        }
    };
    vm.runInNewContext(source, sandbox, { filename: 'tabs.js' });
    return sandbox.window.SilkitTabs.create(ctx);
}

function assertEmptyVisible(label) {
    assert.strictEqual(ids.workspaceEmpty.hidden, false, label + '：空态应可见');
    assert.strictEqual(ids.editorPane.hidden, true, label + '：编辑器应隐藏');
    assert.strictEqual(ids.resultsPane.hidden, true, label + '：结果区应隐藏');
    assert.strictEqual(ids.splitter.hidden, true, label + '：分割条应隐藏');
    assert.strictEqual(ids.tablePane.hidden, true, label + '：表格区应隐藏');
}

function assertEmptyHidden(label) {
    assert.strictEqual(ids.workspaceEmpty.hidden, true, label + '：空态应隐藏');
    assert.strictEqual(ids.editorPane.hidden, false, label + '：编辑器应可见');
    assert.strictEqual(ids.resultsPane.hidden, false, label + '：结果区应可见');
}

// ---------------------------------------------------------------- 用例
async function main() {
console.log('Silkit 标签空态回归自测');

await check('启动时无标签：不自动创建查询页，停在空态', function () {
    const env = makeContext();
    const tabs = loadTabs(env.ctx);
    tabs.init();
    assert.strictEqual(env.state.tabs.length, 0, '启动后不应有任何标签');
    assertEmptyVisible('启动');
});

await check('新建查询：隐藏空态并恢复编辑器', async function () {
    const env = makeContext();
    const tabs = loadTabs(env.ctx);
    tabs.init();
    const tab = tabs.openQuery(null, { sql: 'SELECT 1' });
    assert.strictEqual(env.state.tabs.length, 1);
    assert.strictEqual(env.state.activeTabId, tab.id);
    assertEmptyHidden('新建查询');
});

await check('关闭最后一个标签：彻底关闭，不再弹新查询页', async function () {
    const env = makeContext();
    const tabs = loadTabs(env.ctx);
    tabs.init();
    const tab = tabs.openQuery(null, { sql: 'SELECT 1' });
    assertEmptyHidden('关闭前');
    await tabs.closeTab(tab.id);
    assert.strictEqual(env.state.tabs.length, 0, '关闭后不应残留或新生标签');
    assert.strictEqual(env.state.activeTabId, null, '激活标签应清空');
    assertEmptyVisible('关闭后');
});

await check('反复开关仍保持可彻底关闭', async function () {
    const env = makeContext();
    const tabs = loadTabs(env.ctx);
    tabs.init();
    for (let i = 0; i < 3; i += 1) {
        const tab = tabs.openQuery(null, { sql: 'SELECT ' + i });
        assertEmptyHidden('第 ' + (i + 1) + ' 次新建');
        await tabs.closeTab(tab.id);
        assert.strictEqual(env.state.tabs.length, 0);
        assertEmptyVisible('第 ' + (i + 1) + ' 次关闭');
    }
});

await check('多个标签：关闭非激活标签不误切换，关到空才显示空态', async function () {
    const env = makeContext();
    const tabs = loadTabs(env.ctx);
    tabs.init();
    const first = tabs.openQuery(null, { sql: 'SELECT 1' });
    const second = tabs.openQuery(null, { sql: 'SELECT 2' });
    assert.strictEqual(env.state.activeTabId, second.id);
    await tabs.closeTab(first.id);
    assert.strictEqual(env.state.tabs.length, 1, '应只剩一个标签');
    assert.strictEqual(env.state.activeTabId, second.id, '激活标签不应被误切到已关闭的标签');
    assertEmptyHidden('还剩一个标签');
    await tabs.closeTab(second.id);
    assert.strictEqual(env.state.tabs.length, 0);
    assertEmptyVisible('全部关闭');
});

console.log('\n通过 ' + passed + ' 项');
}

main().catch(function (error) {
    console.error(error && error.stack ? error.stack : error);
    process.exit(1);
});
