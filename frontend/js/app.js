// 应用启动与 UI 交互：主题面板、快捷键、自检、标签、分栏、对象树、Toast。
(function () {
    'use strict';

    const api = window.SilkitApi;
    const themes = window.SilkitThemes;
    const bridge = window.SilkitBridge;

    const $ = function (id) { return document.getElementById(id); };

    let pingTask = null;
    let panelOpen = false;
    let logCount = 0;
    let editorRatio = 0.44;
    let saveTimer = 0;
    let sidebarHidden = false;

    // ------------------------------------------------------------------ Toast
    function toast(message, variant) {
        const host = $('toastHost');
        if (!host) return;

        const node = document.createElement('div');
        node.className = 'toast' + (variant ? ' toast-' + variant : '');
        node.innerHTML = (window.Icons ? window.Icons.svg('sparkle', 16) : '') +
            '<span class="toast-text"></span>';
        node.querySelector('.toast-text').textContent = message;
        host.appendChild(node);

        setTimeout(function () {
            node.classList.add('is-out');
            setTimeout(function () { node.remove(); }, 220);
        }, 2800);
    }

    // ------------------------------------------------------------ 主题面板
    function openPanel() {
        const panel = $('themePanel');
        const trigger = $('themeBtn');
        if (!panel || panelOpen) return;
        panelOpen = true;
        panel.hidden = false;
        if (trigger) trigger.setAttribute('aria-expanded', 'true');
        themes.setFocus(themes.indexOf(themes.current().id));
    }

    function closePanel() {
        const panel = $('themePanel');
        const trigger = $('themeBtn');
        if (!panel || !panelOpen) return;
        panelOpen = false;
        panel.hidden = true;
        if (trigger) trigger.setAttribute('aria-expanded', 'false');
    }

    function togglePanel() {
        if (panelOpen) closePanel();
        else openPanel();
    }

    // ------------------------------------------------------------ 消息面板
    function logTime() {
        const now = new Date();
        return [now.getHours(), now.getMinutes(), now.getSeconds()]
            .map(function (value) { return String(value).padStart(2, '0'); })
            .join(':');
    }

    function addLog(level, text) {
        const list = $('logList');
        if (!list) return;

        const line = document.createElement('li');
        line.className = 'log-line';
        line.setAttribute('data-level', level);

        const time = document.createElement('span');
        time.className = 'log-time';
        time.textContent = logTime();

        const tag = document.createElement('span');
        tag.className = 'log-tag';
        tag.textContent = level;

        const body = document.createElement('span');
        body.className = 'log-text';
        body.textContent = text;

        line.appendChild(time);
        line.appendChild(tag);
        line.appendChild(body);
        list.appendChild(line);

        while (list.children.length > 200) list.removeChild(list.firstElementChild);

        logCount += 1;
        const counter = $('msgCount');
        if (counter) {
            counter.hidden = false;
            counter.textContent = String(logCount);
        }

        const empty = document.querySelector('.log-empty');
        if (empty) empty.hidden = true;

        list.scrollTop = list.scrollHeight;
    }

    function showResultTab(targetId) {
        const tabs = document.querySelectorAll('.rtab');
        for (let i = 0; i < tabs.length; i += 1) {
            const active = tabs[i].getAttribute('data-target') === targetId;
            tabs[i].classList.toggle('active', active);
            tabs[i].setAttribute('aria-selected', active ? 'true' : 'false');
        }
        const panels = document.querySelectorAll('.result-body > [role="tabpanel"]');
        for (let i = 0; i < panels.length; i += 1) {
            panels[i].hidden = panels[i].id !== targetId;
        }
    }

    // ------------------------------------------------------------ 自检（异步 IPC）
    function setBusy(busy) {
        const status = $('ipcStatus');
        const button = $('pingBtn');
        const label = $('pingLabel');

        if (status) status.textContent = busy ? 'IPC 执行中…' : 'IPC 就绪';
        if (button) button.setAttribute('aria-pressed', busy ? 'true' : 'false');
        if (label) label.textContent = busy ? '取消' : '自检';
    }

    async function runPing() {
        if (pingTask) {
            pingTask.cancel();
            return;
        }

        showResultTab('messageLog');
        setBusy(true);

        const task = api.start('app.ping', { steps: 3, delayMs: 200 }, {
            onProgress: function (data) {
                addLog('progress', '步骤 ' + data.step + '/' + data.total + ' · ' + data.message);
            }
        });
        pingTask = task;

        try {
            const result = await task.promise;
            addLog('result', 'pong · ' + result.steps + ' 步 / ' + result.elapsedMs + ' ms');
            toast('异步 IPC 往返正常（' + result.elapsedMs + ' ms）');
        } catch (error) {
            if (error.code === 'cancelled') {
                addLog('error', '请求已取消（reqId=' + task.reqId + '）');
                toast('已取消自检');
            } else {
                addLog('error', error.message || String(error));
                toast('自检失败：' + (error.message || error.code), 'error');
            }
        } finally {
            pingTask = null;
            setBusy(false);
        }
    }

    // ------------------------------------------------------------ 主标签
    const tabStore = new Map();

    function activateTab(tab) {
        const editor = $('sqlEditor');
        const previous = document.querySelector('.tabbar .tab.active');
        if (editor && previous && previous !== tab) tabStore.set(previous, editor.value);

        const tabs = document.querySelectorAll('.tabbar .tab');
        for (let i = 0; i < tabs.length; i += 1) {
            const active = tabs[i] === tab;
            tabs[i].classList.toggle('active', active);
            tabs[i].setAttribute('aria-selected', active ? 'true' : 'false');
        }

        const title = document.querySelector('.editor-title');
        if (title) title.textContent = tab.querySelector('.tab-label').textContent;

        if (editor) {
            if (tabStore.has(tab)) {
                editor.value = tabStore.get(tab);
            } else if (!tab.classList.contains('is-persistent')) {
                editor.value = '';
            }
            updateGutter();
        }
    }

    function addTab() {
        const bar = document.querySelector('.tabbar');
        if (!bar) return;

        const count = bar.querySelectorAll('.tab').length + 1;
        const tab = document.createElement('button');
        tab.className = 'tab';
        tab.type = 'button';
        tab.setAttribute('role', 'tab');
        tab.setAttribute('aria-selected', 'false');
        tab.innerHTML = (window.Icons ? window.Icons.svg('terminal', 14) : '') +
            '<span class="tab-label">查询 ' + count + '</span><span class="tab-dot"></span>';

        bar.insertBefore(tab, bar.querySelector('.tab-add'));
        activateTab(tab);
    }

    function updateGutter() {
        const editor = $('sqlEditor');
        const gutter = document.querySelector('.line-gutter');
        if (!editor || !gutter) return;
        const lines = editor.value.split('\n').length;
        const numbers = [];
        for (let i = 1; i <= Math.max(lines, 1); i += 1) numbers.push(i);
        gutter.textContent = numbers.join('\n');
        gutter.scrollTop = editor.scrollTop;
    }

    // ------------------------------------------------------------ 对象树
    function toggleNode(node) {
        const children = node.nextElementSibling;
        if (!children || !children.classList.contains('tree-children')) return false;
        const collapsed = !children.hidden;
        children.hidden = collapsed;
        const caret = node.querySelector('.caret');
        if (caret) caret.classList.toggle('open', !collapsed);
        return true;
    }

    function filterNode(node, query) {
        const labelEl = node.querySelector('.node-label');
        const label = labelEl ? labelEl.textContent.toLowerCase() : '';
        const selfMatch = !query || label.indexOf(query) >= 0;

        let childMatch = false;
        const container = node.nextElementSibling;
        if (container && container.classList.contains('tree-children') && query) {
            const children = container.children;
            for (let i = 0; i < children.length; i += 1) {
                if (children[i].classList.contains('tree-node')) {
                    if (filterNode(children[i], query)) childMatch = true;
                }
            }
            if (childMatch) {
                container.hidden = false;
                const caret = node.querySelector('.caret');
                if (caret) caret.classList.add('open');
            }
        }

        node.hidden = !(selfMatch || childMatch);
        return selfMatch || childMatch;
    }

    function filterTree(rawQuery) {
        const query = (rawQuery || '').trim().toLowerCase();
        const tree = $('objectTree');
        if (!tree) return;

        const groups = tree.querySelectorAll('.tree-group');
        for (let i = 0; i < groups.length; i += 1) {
            const nodes = groups[i].querySelectorAll(':scope > .tree-node');
            let any = false;
            for (let j = 0; j < nodes.length; j += 1) {
                if (filterNode(nodes[j], query)) any = true;
            }
            groups[i].hidden = !!query && !any;
        }

        if (!query) {
            const all = tree.querySelectorAll('.tree-node');
            for (let i = 0; i < all.length; i += 1) all[i].hidden = false;
        }
    }

    // ------------------------------------------------------------ 分栏
    function applyEditorRatio(ratio) {
        const pane = $('editorPane');
        if (!pane) return;
        editorRatio = Math.min(0.78, Math.max(0.18, Number(ratio) || 0.44));
        pane.style.setProperty('--editor-h', (editorRatio * 100).toFixed(2) + '%');
    }

    function persistEditorRatio() {
        clearTimeout(saveTimer);
        saveTimer = setTimeout(function () {
            api.settingsSet({ ui: { editorRatio: Number(editorRatio.toFixed(3)) } }).catch(function () { /* ignore */ });
        }, 400);
    }

    function bindSplitter() {
        const splitter = $('splitter');
        const pane = $('editorPane');
        const main = document.querySelector('.main');
        if (!splitter || !pane || !main) return;

        let dragging = false;

        function onMove(event) {
            if (!dragging) return;
            const paneRect = pane.getBoundingClientRect();
            const mainRect = main.getBoundingClientRect();
            const height = event.clientY - paneRect.top;
            const min = 120;
            const max = mainRect.height - 160;
            const clamped = Math.min(max, Math.max(min, height));
            pane.style.setProperty('--editor-h', clamped + 'px');
            editorRatio = clamped / mainRect.height;
        }

        function stop() {
            if (!dragging) return;
            dragging = false;
            splitter.classList.remove('is-dragging');
            document.body.style.cursor = '';
            document.body.style.userSelect = '';
            persistEditorRatio();
        }

        splitter.addEventListener('pointerdown', function (event) {
            if (event.button !== 0) return;
            dragging = true;
            splitter.classList.add('is-dragging');
            document.body.style.cursor = 'row-resize';
            document.body.style.userSelect = 'none';
            splitter.setPointerCapture(event.pointerId);
            event.preventDefault();
        });

        splitter.addEventListener('pointermove', onMove);
        splitter.addEventListener('pointerup', stop);
        splitter.addEventListener('pointercancel', stop);

        splitter.addEventListener('keydown', function (event) {
            if (event.key !== 'ArrowUp' && event.key !== 'ArrowDown') return;
            event.preventDefault();
            const step = event.key === 'ArrowUp' ? -0.03 : 0.03;
            applyEditorRatio(editorRatio + step);
            persistEditorRatio();
        });
    }

    // ------------------------------------------------------------ 侧栏
    function toggleSidebar() {
        const sidebar = document.querySelector('.sidebar');
        if (!sidebar) return;
        sidebarHidden = !sidebarHidden;
        sidebar.classList.toggle('is-collapsed', sidebarHidden);
        toast(sidebarHidden ? '侧栏已收起（Ctrl+B 恢复）' : '侧栏已展开');
    }

    // ------------------------------------------------------------ 快捷键
    function isField(target) {
        if (!target) return false;
        const tag = target.tagName;
        return tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT' || target.isContentEditable;
    }

    function bindKeys() {
        document.addEventListener('keydown', function (event) {
            const mod = event.ctrlKey || event.metaKey;

            if (mod && event.shiftKey && event.code === 'KeyT') {
                event.preventDefault();
                togglePanel();
                return;
            }
            if (mod && event.shiftKey && event.code === 'KeyL') {
                event.preventDefault();
                const theme = themes.toggle();
                toast('外观：' + theme.name + ' ' + theme.en);
                return;
            }
            if (mod && !event.shiftKey && event.code === 'KeyB') {
                event.preventDefault();
                toggleSidebar();
                return;
            }

            if (panelOpen) {
                if (event.key === 'Escape') {
                    event.preventDefault();
                    closePanel();
                    return;
                }
                if (event.key === 'ArrowDown') {
                    event.preventDefault();
                    themes.moveFocus(1);
                    return;
                }
                if (event.key === 'ArrowUp') {
                    event.preventDefault();
                    themes.moveFocus(-1);
                    return;
                }
                if (event.key === 'Enter') {
                    event.preventDefault();
                    const theme = themes.applyFocused();
                    toast('外观：' + theme.name + ' ' + theme.en);
                    return;
                }
                if (!mod && !isField(event.target) && /^[1-5]$/.test(event.key)) {
                    event.preventDefault();
                    const theme = themes.apply(themes.THEMES[Number(event.key) - 1].id);
                    themes.setFocus(Number(event.key) - 1);
                    toast('外观：' + theme.name + ' ' + theme.en);
                    return;
                }
            }

            if (event.key === 'Escape' && pingTask) {
                event.preventDefault();
                pingTask.cancel();
            }
        });
    }

    // ------------------------------------------------------------ 启动
    async function boot() {
        if (window.Icons) window.Icons.mount(document);

        bindKeys();
        bindSplitter();

        $('themeBtn').addEventListener('click', function (event) {
            event.stopPropagation();
            togglePanel();
        });

        $('pingBtn').addEventListener('click', runPing);
        $('runBtn').addEventListener('click', function () {
            addLog('progress', 'M0 只验证异步通道；SQL 执行引擎将在 M4 接入。');
            showResultTab('messageLog');
            toast('M0 演示：SQL 执行在 M4 接入');
        });

        document.addEventListener('pointerdown', function (event) {
            if (!panelOpen) return;
            if (event.target.closest('#themePanel') || event.target.closest('#themeBtn')) return;
            closePanel();
        });

        document.querySelectorAll('.rtab').forEach(function (tab) {
            tab.addEventListener('click', function () {
                showResultTab(tab.getAttribute('data-target'));
            });
        });

        document.querySelectorAll('.tabbar .tab').forEach(function (tab) {
            tab.classList.add('is-persistent');
            tab.addEventListener('click', function () { activateTab(tab); });
        });

        document.querySelector('.tab-add').addEventListener('click', addTab);

        const editor = $('sqlEditor');
        editor.addEventListener('input', function () {
            const active = document.querySelector('.tabbar .tab.active');
            if (active) tabStore.set(active, editor.value);
            updateGutter();
        });
        editor.addEventListener('scroll', function () {
            const gutter = document.querySelector('.line-gutter');
            if (gutter) gutter.scrollTop = editor.scrollTop;
        });
        updateGutter();

        document.querySelectorAll('.tabbar .tab').forEach(function (tab) {
            tabStore.set(tab, tab.classList.contains('active') ? editor.value : '');
        });

        const tree = $('objectTree');
        tree.addEventListener('click', function (event) {
            const node = event.target.closest('.tree-node');
            if (!node) return;
            tree.querySelectorAll('.tree-node.is-active').forEach(function (active) {
                active.classList.remove('is-active');
            });
            node.classList.add('is-active');
            toggleNode(node);
        });

        $('treeFilter').addEventListener('input', function (event) {
            filterTree(event.target.value);
        });

        setBusy(false);

        try {
            const info = await api.info();
            const version = 'v' + (info.version || '0.0.1');
            if ($('versionChip')) $('versionChip').textContent = version;
            if ($('statusVersion')) $('statusVersion').textContent = version;
            if ($('versionChip') && info.dataDir) $('versionChip').title = '数据目录：' + info.dataDir;
        } catch (error) {
            if ($('ipcStatus')) $('ipcStatus').textContent = 'IPC 不可用';
            toast('IPC 初始化失败：' + (error.message || error.code), 'error');
        }

        await themes.init();

        try {
            const result = await api.settingsGet();
            const settings = result && result.settings ? result.settings : null;
            if (settings && settings.ui && settings.ui.editorRatio) {
                applyEditorRatio(settings.ui.editorRatio);
            }
        } catch (error) { /* 已在 themes.init 中提示过 */ }

        document.addEventListener('silkit:theme', function (event) {
            if (bridge.isNative) {
                api.log('info', '主题切换为 ' + event.detail.id).catch(function () { /* ignore */ });
            }
        });

        if (!bridge.isNative) {
            const tag = document.querySelector('.brand-tag');
            if (tag) tag.textContent = 'M0 · Mock';
            if ($('ipcStatus')) $('ipcStatus').textContent = 'IPC 就绪（浏览器 Mock）';
        }
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', boot);
    } else {
        boot();
    }
})();
