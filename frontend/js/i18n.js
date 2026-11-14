// 轻量 i18n：字典 + data-i18n 属性替换。
//
//   SilkitI18n.t('settings.title')
//   SilkitI18n.setLanguage('en-US')   // 会立即重刷 [data-i18n] 元素
//
// 覆盖外壳、设置对话框与常用对话框；视图内部动态文案仍以中文为主（见 CODEPLAN 偏差记录）。
(function () {
    'use strict';

    const dict = {
        'zh-CN': {
            'app.brandTag': 'M8',
            'topbar.newConn': '新建连接',
            'topbar.newQuery': '新建查询',
            'topbar.openSql': '打开 SQL',
            'topbar.runFile': '执行 SQL 文件',
            'topbar.history': '最近',
            'topbar.settings': '设置',
            'theme.button': '主题',

            'sidebar.search': '搜索对象…',
            'sidebar.refresh': '刷新对象树',
            'sidebar.emptyTitle': '还没有连接',
            'sidebar.emptyHint': '新建连接时可选择 MySQL、PostgreSQL 或 SQLite',
            'sidebar.openConnection': '打开数据库连接',
            'sidebar.notConnected': '尚未连接',

            'tabbar.new': '新建查询标签',
            'editor.title': '查询 1',
            'editor.sub': '未连接 · 打开连接后即可执行',
            'editor.scope': '执行范围',
            'editor.scopeAll': '全部语句',
            'editor.scopeCurrent': '当前语句',
            'editor.format': '格式化',
            'editor.explain': 'EXPLAIN',
            'editor.run': '运行',
            'editor.cancel': '取消',

            'results.tabResult': '结果',
            'results.tabPlan': '计划',
            'results.tabMessage': '消息',
            'results.exportCsv': '导出 CSV',
            'results.exportJson': '导出 JSON',
            'results.copy': '复制结果',
            'results.emptyPlan': '还没有执行计划',
            'results.emptyPlanHint': '选中语句后点击 EXPLAIN，或在计划面板查看最近一次输出。',
            'results.emptyMessage': '还没有消息',
            'results.emptyMessageHint': '执行 SQL 后，这里显示语句耗时、行数与错误定位。',
            'results.ready': '就绪',
            'results.rows': '{n} 行',
            'results.hint': '双击单元格查看全文 · Ctrl+C 复制选区',

            'status.mode': 'M8 · 等待连接',
            'status.modeConnected': 'M8 · 已连接',
            'status.ipc': 'IPC 就绪',
            'status.busy': 'IPC 执行中…',
            'status.perf': '{ms} ms · 内存 {mem}',

            'settings.title': '设置',
            'settings.tabGeneral': '通用',
            'settings.tabEditor': '编辑器',
            'settings.tabQuery': '查询',
            'settings.tabGrid': '数据网格',
            'settings.tabExport': '导出',
            'settings.tabAbout': '关于',
            'settings.language': '界面语言',
            'settings.languageHint': '切换后立即生效，重启后保持。',
            'settings.appearance': '外观',
            'settings.reduceMotion': '减少动画',
            'settings.reduceMotionHint': '关闭面板与列表的过渡动画。',
            'settings.gridRowHeight': '网格行高',
            'settings.sidebarWidth': '侧栏宽度',
            'settings.restoreTabs': '启动时恢复标签',
            'settings.restoreTabsHint': '恢复上次打开的查询与表数据标签。',
            'settings.confirmDestructive': '危险操作二次确认',
            'settings.confirmDestructiveHint': '删除表 / 清空表 / 删除库前要求输入对象名。',
            'settings.editorFontSize': '字号',
            'settings.editorTabSize': '缩进宽度',
            'settings.editorWordWrap': '自动换行',
            'settings.editorLineNumbers': '显示行号',
            'settings.editorAutocomplete': '自动补全',
            'settings.querySafeMode': '安全模式',
            'settings.querySafeModeHint': '执行 DROP / TRUNCATE / ALTER / 无 WHERE 的 UPDATE、DELETE 前要求确认。',
            'settings.queryMaxRows': '默认最大行数',
            'settings.queryFetchSize': '分批大小',
            'settings.queryAutoLimit': '自动追加 LIMIT',
            'settings.queryHistoryLimit': '历史条数上限',
            'settings.gridPageSize': '表格每页行数',
            'settings.gridNullDisplay': 'NULL 显示文本',
            'settings.gridMaxCell': '单元格最大字节',
            'settings.exportEncoding': '默认编码',
            'settings.exportDelimiter': 'CSV 分隔符',
            'settings.exportHeader': '默认包含表头',
            'settings.exportNull': 'NULL 字面量',
            'settings.aboutVersion': '版本',
            'settings.aboutDataDir': '数据目录',
            'settings.aboutLogsDir': '日志目录',
            'settings.aboutSettings': '配置文件',
            'settings.aboutLogFile': '当前日志',
            'settings.openData': '打开数据目录',
            'settings.openLogs': '打开日志目录',
            'settings.reset': '恢复默认',
            'settings.cancel': '取消',
            'settings.save': '保存',
            'settings.unsaved': '有未保存的修改',
            'settings.saved': '设置已保存',
            'settings.resetConfirm': '恢复默认设置？当前配置会备份为 settings.json.bak。',
            'settings.resetDone': '已恢复默认设置',
            'settings.openFailed': '无法打开目录',

            'common.close': '关闭',
            'common.ok': '确定',
            'common.cancel': '取消',
            'common.confirm': '确认',
            'common.retry': '重试',
            'common.loading': '加载中…',
            'danger.title': '确认执行危险语句',
            'danger.message': '安全模式检测到 {n} 条危险语句，执行后可能无法撤销。',
            'danger.confirm': '仍然执行',
            'danger.cancel': '取消'
        },
        'en-US': {
            'app.brandTag': 'M8',
            'topbar.newConn': 'New Connection',
            'topbar.newQuery': 'New Query',
            'topbar.openSql': 'Open SQL',
            'topbar.runFile': 'Run SQL File',
            'topbar.history': 'Recent',
            'topbar.settings': 'Settings',
            'theme.button': 'Theme',

            'sidebar.search': 'Search objects…',
            'sidebar.refresh': 'Refresh object tree',
            'sidebar.emptyTitle': 'No connections yet',
            'sidebar.emptyHint': 'Create a connection to MySQL, PostgreSQL or SQLite',
            'sidebar.openConnection': 'Open Database Connection',
            'sidebar.notConnected': 'Not connected',

            'tabbar.new': 'New query tab',
            'editor.title': 'Query 1',
            'editor.sub': 'Not connected · open a connection to run',
            'editor.scope': 'Execution scope',
            'editor.scopeAll': 'All statements',
            'editor.scopeCurrent': 'Current statement',
            'editor.format': 'Format',
            'editor.explain': 'EXPLAIN',
            'editor.run': 'Run',
            'editor.cancel': 'Cancel',

            'results.tabResult': 'Results',
            'results.tabPlan': 'Plan',
            'results.tabMessage': 'Messages',
            'results.exportCsv': 'Export CSV',
            'results.exportJson': 'Export JSON',
            'results.copy': 'Copy results',
            'results.emptyPlan': 'No execution plan yet',
            'results.emptyPlanHint': 'Select a statement and click EXPLAIN, or check the last output here.',
            'results.emptyMessage': 'No messages yet',
            'results.emptyMessageHint': 'After running SQL, statement time, rows and errors appear here.',
            'results.ready': 'Ready',
            'results.rows': '{n} rows',
            'results.hint': 'Double-click a cell to view · Ctrl+C copies selection',

            'status.mode': 'M8 · Waiting for connection',
            'status.modeConnected': 'M8 · Connected',
            'status.ipc': 'IPC ready',
            'status.busy': 'IPC busy…',
            'status.perf': '{ms} ms · memory {mem}',

            'settings.title': 'Settings',
            'settings.tabGeneral': 'General',
            'settings.tabEditor': 'Editor',
            'settings.tabQuery': 'Query',
            'settings.tabGrid': 'Data Grid',
            'settings.tabExport': 'Export',
            'settings.tabAbout': 'About',
            'settings.language': 'Language',
            'settings.languageHint': 'Applies immediately and persists across restarts.',
            'settings.appearance': 'Appearance',
            'settings.reduceMotion': 'Reduce motion',
            'settings.reduceMotionHint': 'Disable panel and list transitions.',
            'settings.gridRowHeight': 'Row height',
            'settings.sidebarWidth': 'Sidebar width',
            'settings.restoreTabs': 'Restore tabs on startup',
            'settings.restoreTabsHint': 'Reopen queries and table tabs from last session.',
            'settings.confirmDestructive': 'Confirm destructive actions',
            'settings.confirmDestructiveHint': 'Ask for the object name before dropping or truncating.',
            'settings.editorFontSize': 'Font size',
            'settings.editorTabSize': 'Tab size',
            'settings.editorWordWrap': 'Word wrap',
            'settings.editorLineNumbers': 'Show line numbers',
            'settings.editorAutocomplete': 'Autocomplete',
            'settings.querySafeMode': 'Safe mode',
            'settings.querySafeModeHint': 'Confirm before DROP / TRUNCATE / ALTER / UPDATE or DELETE without WHERE.',
            'settings.queryMaxRows': 'Default max rows',
            'settings.queryFetchSize': 'Fetch size',
            'settings.queryAutoLimit': 'Append LIMIT automatically',
            'settings.queryHistoryLimit': 'History limit',
            'settings.gridPageSize': 'Rows per page',
            'settings.gridNullDisplay': 'NULL display',
            'settings.gridMaxCell': 'Max cell bytes',
            'settings.exportEncoding': 'Default encoding',
            'settings.exportDelimiter': 'CSV delimiter',
            'settings.exportHeader': 'Include header by default',
            'settings.exportNull': 'NULL literal',
            'settings.aboutVersion': 'Version',
            'settings.aboutDataDir': 'Data directory',
            'settings.aboutLogsDir': 'Log directory',
            'settings.aboutSettings': 'Settings file',
            'settings.aboutLogFile': 'Current log',
            'settings.openData': 'Open data directory',
            'settings.openLogs': 'Open log directory',
            'settings.reset': 'Restore defaults',
            'settings.cancel': 'Cancel',
            'settings.save': 'Save',
            'settings.unsaved': 'Unsaved changes',
            'settings.saved': 'Settings saved',
            'settings.resetConfirm': 'Restore default settings? The current file is kept as settings.json.bak.',
            'settings.resetDone': 'Defaults restored',
            'settings.openFailed': 'Could not open the directory',

            'common.close': 'Close',
            'common.ok': 'OK',
            'common.cancel': 'Cancel',
            'common.confirm': 'Confirm',
            'common.retry': 'Retry',
            'common.loading': 'Loading…',
            'danger.title': 'Confirm destructive statements',
            'danger.message': 'Safe mode found {n} destructive statement(s). This may not be undoable.',
            'danger.confirm': 'Run anyway',
            'danger.cancel': 'Cancel'
        }
    };

    let language = 'zh-CN';
    const listeners = [];

    function normalize(value) {
        return /^en/i.test(String(value || '')) ? 'en-US' : 'zh-CN';
    }

    function table() {
        return dict[language] || dict['zh-CN'];
    }

    function t(key, vars) {
        let text = table()[key];
        if (text === undefined) text = dict['zh-CN'][key];
        if (text === undefined) return key;
        if (vars && typeof vars === 'object') {
            text = text.replace(/\{(\w+)\}/g, function (match, name) {
                return Object.prototype.hasOwnProperty.call(vars, name) ? String(vars[name]) : match;
            });
        }
        return text;
    }

    function apply(root) {
        const scope = root || document;
        if (!scope.querySelectorAll) return;
        scope.querySelectorAll('[data-i18n]').forEach(function (el) {
            el.textContent = t(el.getAttribute('data-i18n'));
        });
        scope.querySelectorAll('[data-i18n-title]').forEach(function (el) {
            el.setAttribute('title', t(el.getAttribute('data-i18n-title')));
        });
        scope.querySelectorAll('[data-i18n-placeholder]').forEach(function (el) {
            el.setAttribute('placeholder', t(el.getAttribute('data-i18n-placeholder')));
        });
        scope.querySelectorAll('[data-i18n-aria]').forEach(function (el) {
            el.setAttribute('aria-label', t(el.getAttribute('data-i18n-aria')));
        });
    }

    function setLanguage(value, options) {
        language = normalize(value);
        document.documentElement.lang = language;
        apply(document);
        if (!options || options.notify !== false) {
            listeners.forEach(function (fn) {
                try {
                    fn(language);
                } catch (error) {
                    console.error('[silkit] i18n listener', error);
                }
            });
        }
    }

    function current() {
        return language;
    }

    function onChange(fn) {
        if (typeof fn === 'function') listeners.push(fn);
    }

    window.SilkitI18n = {
        t: t,
        apply: apply,
        setLanguage: setLanguage,
        current: current,
        normalize: normalize,
        onChange: onChange
    };
})();
