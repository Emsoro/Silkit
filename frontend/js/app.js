// 应用启动与外壳交互：主题面板、分栏、侧栏、自检、快捷键；
// 数据库工作台逻辑委托给 workbench.js 及 views/*。
(function () {
    'use strict';

    const api = window.SilkitApi;
    const themes = window.SilkitThemes;
    const bridge = window.SilkitBridge;
    const state = window.SilkitState;

    const $ = function (id) { return document.getElementById(id); };


    let panelOpen = false;
    let editorRatio = 0.44;
    let saveTimer = 0;
    let sidebarHidden = false;
    let workbench = null;

    // ------------------------------------------------------------------ Toast
    // options.copyText：错误提示可附带「复制信息」按钮，并延长停留时间以便点击。
    function toast(message, variant, options) {
        const host = $('toastHost');
        if (!host) return;
        const copyValue = options && options.copyText ? String(options.copyText) : '';

        const node = document.createElement('div');
        node.className = 'toast' + (variant ? ' toast-' + variant : '');
        node.innerHTML = (window.Icons ? window.Icons.svg(variant === 'error' ? 'warning' : 'sparkle', 16) : '') +
            '<span class="toast-text"></span>';
        node.querySelector('.toast-text').textContent = message;
        if (copyValue) {
            const button = document.createElement('button');
            button.type = 'button';
            button.className = 'toast-copy';
            button.textContent = '复制信息';
            button.title = '复制完整错误信息与连接诊断（不含密码）';
            button.addEventListener('click', function () {
                const copy = window.SilkitUtil && window.SilkitUtil.copyText;
                if (!copy) return;
                Promise.resolve(copy(copyValue)).then(function () {
                    button.textContent = '已复制';
                }).catch(function () {
                    button.textContent = '复制失败';
                });
            });
            node.appendChild(button);
        }
        host.appendChild(node);

        setTimeout(function () {
            node.classList.add('is-out');
            setTimeout(function () { node.remove(); }, 220);
        }, copyValue ? 8000 : 2800);
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

    // ------------------------------------------------------------ IPC 忙碌指示
    function setBusy(busy) {
        const status = $('ipcStatus');
        if (!status) return;
        const I = window.SilkitI18n;
        status.textContent = busy ? (I ? I.t('status.busy') : 'IPC 执行中…')
                                  : (I ? I.t('status.ipc') : 'IPC 就绪');
    }

    function activeResults() {
        return workbench && workbench.app ? workbench.app.results : null;
    }

    function activeTab() {
        return state ? state.activeTab() : null;
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
            api.settingsSet({ ui: { editorRatio: Number(editorRatio.toFixed(3)) } })
                .catch(function () { /* ignore */ });
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
    function applySidebarWidth(width) {
        const value = Number(width);
        if (!Number.isFinite(value)) return;
        document.documentElement.style.setProperty(
            '--sidebar-w', Math.max(180, Math.min(560, value)) + 'px');
    }

    function applySidebarHidden(hidden) {
        sidebarHidden = !!hidden;
        const sidebar = document.querySelector('.sidebar');
        if (sidebar) sidebar.classList.toggle('is-collapsed', sidebarHidden);
        const splitter = $('sidebarSplitter');
        if (splitter) splitter.hidden = sidebarHidden;
    }

    function persistSidebar(patch) {
        api.settingsSet({ ui: patch }).catch(function () { /* ignore */ });
    }

    function toggleSidebar() {
        if (!document.querySelector('.sidebar')) return;
        applySidebarHidden(!sidebarHidden);
        persistSidebar({ sidebarHidden: sidebarHidden });
        toast(sidebarHidden ? '侧栏已收起（Ctrl+B 恢复）' : '侧栏已展开');
    }

    function bindSidebarSplitter() {
        const splitter = $('sidebarSplitter');
        const sidebar = document.querySelector('.sidebar');
        if (!splitter || !sidebar) return;
        let dragging = false;

        function onMove(event) {
            if (!dragging) return;
            const left = sidebar.getBoundingClientRect().left;
            applySidebarWidth(event.clientX - left);
        }

        function stop() {
            if (!dragging) return;
            dragging = false;
            splitter.classList.remove('is-dragging');
            document.body.style.cursor = '';
            document.body.style.userSelect = '';
            const width = Math.round(sidebar.getBoundingClientRect().width);
            persistSidebar({ sidebarWidth: width });
        }

        splitter.addEventListener('pointerdown', function (event) {
            if (event.button !== 0 || sidebarHidden) return;
            dragging = true;
            splitter.classList.add('is-dragging');
            document.body.style.cursor = 'col-resize';
            document.body.style.userSelect = 'none';
            splitter.setPointerCapture(event.pointerId);
            event.preventDefault();
        });
        splitter.addEventListener('pointermove', onMove);
        splitter.addEventListener('pointerup', stop);
        splitter.addEventListener('pointercancel', stop);

        splitter.addEventListener('keydown', function (event) {
            if (event.key !== 'ArrowLeft' && event.key !== 'ArrowRight') return;
            event.preventDefault();
            const current = sidebar.getBoundingClientRect().width;
            const next = current + (event.key === 'ArrowLeft' ? -16 : 16);
            applySidebarWidth(next);
            persistSidebar({ sidebarWidth: Math.round(Math.max(180, Math.min(560, next))) });
        });
    }

    // ------------------------------------------------------------ 快捷键
    function isField(target) {
        if (!target) return false;
        const tag = target.tagName;
        return tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT' || target.isContentEditable;
    }

    function modalOpen() {
        const ids = ['connDialog', 'cellViewer', 'detailDialog', 'ioDialog',
                     'settingsDialog', 'ddlConfirm', 'designerDialog', 'appConfirm'];
        for (let i = 0; i < ids.length; i += 1) {
            const node = $(ids[i]);
            if (node && !node.hidden) return true;
        }
        return false;
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
            if (mod && !event.shiftKey && event.code === 'KeyN') {
                event.preventDefault();
                if (workbench) workbench.app.tabs.openQueryFromTree();
                return;
            }
            if (mod && !event.shiftKey && event.code === 'KeyW') {
                event.preventDefault();
                if (workbench) workbench.app.tabs.closeActive();
                return;
            }
            if ((mod && event.code === 'Enter') || event.code === 'F5') {
                if (modalOpen()) return;
                event.preventDefault();
                const tab = activeTab();
                if (workbench && tab && tab.kind === 'table') return;
                if (workbench && tab) {
                    workbench.app.query.run(tab, event.shiftKey ? 'current' : undefined);
                }
                return;
            }
            if (mod && !event.shiftKey && event.code === 'KeyS') {
                if (modalOpen()) return;
                event.preventDefault();
                if (!workbench) return;
                const tab = activeTab();
                if (tab && tab.kind === 'table') workbench.app.table.save(tab);
                else workbench.app.saveSqlDialog(false);
                return;
            }
            if (event.shiftKey && event.altKey && event.code === 'KeyF') {
                if (modalOpen()) return;
                event.preventDefault();
                const tab = activeTab();
                if (workbench && tab && tab.kind !== 'table') workbench.app.tabs.formatEditor();
                return;
            }
            if (mod && event.shiftKey && event.code === 'KeyP') {
                if (modalOpen()) return;
                event.preventDefault();
                const tab = activeTab();
                if (workbench && tab && tab.kind !== 'table') workbench.app.query.explain(tab);
                return;
            }
            if (event.key === 'Escape') {
                if (modalOpen()) return;
                const tab = activeTab();
                if (workbench && tab && tab.running) {
                    event.preventDefault();
                    workbench.app.query.cancel(tab);
                    return;
                }
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
                if (!mod && !isField(event.target) && /^[1-8]$/.test(event.key)) {
                    event.preventDefault();
                    const theme = themes.apply(themes.THEMES[Number(event.key) - 1].id);
                    themes.setFocus(Number(event.key) - 1);
                    toast('外观：' + theme.name + ' ' + theme.en);
                    return;
                }
            }
        });
    }

    // ------------------------------------------------------------ 启动
    async function boot() {
        if (window.Icons) window.Icons.mount(document);

        bindKeys();
        bindSplitter();
        bindSidebarSplitter();

        const themeBtn = $('themeBtn');
        if (themeBtn) {
            themeBtn.addEventListener('click', function (event) {
                event.stopPropagation();
                togglePanel();
            });
        }

        document.addEventListener('pointerdown', function (event) {
            if (!panelOpen) return;
            if (event.target.closest('#themePanel') || event.target.closest('#themeBtn')) return;
            closePanel();
        });

        let settings = null;
        try {
            await themes.init();
        } catch (error) {
            console.warn('[silkit] 主题初始化失败', error);
        }

        try {
            const result = await api.settingsGet();
            settings = result && result.settings ? result.settings : null;
            if (settings) {
                const ui = settings.ui || {};
                if (window.SilkitI18n) {
                    window.SilkitI18n.setLanguage(ui.language || 'zh-CN', { notify: false });
                }
                applyEditorRatio(ui.editorRatio);
                applySidebarWidth(ui.sidebarWidth);
                applySidebarHidden(ui.sidebarHidden);
            }
        } catch (error) {
            console.warn('[silkit] settings.get 失败', error);
        }

        setBusy(false);

        try {
            workbench = window.SilkitWorkbench.create({ toast: toast });
            await workbench.start(settings);
        } catch (error) {
            console.error('[silkit] 工作台初始化失败', error);
            toast('工作台初始化失败：' + (error.message || error), 'error');
        }

        try {
            const info = await api.info();
            const version = 'v' + (info.version || '1.0.5');
            if ($('statusVersion')) $('statusVersion').textContent = version;
        } catch (error) {
            if ($('ipcStatus')) $('ipcStatus').textContent = 'IPC 不可用';
            toast('IPC 初始化失败：' + (error.message || error.code), 'error');
        }

        if (!bridge.isNative) {
            if ($('ipcStatus')) $('ipcStatus').textContent = 'IPC 就绪（浏览器 Mock）';
        }

        document.addEventListener('silkit:theme', function (event) {
            if (bridge.isNative) {
                api.log('info', '主题切换为 ' + event.detail.id).catch(function () { /* ignore */ });
            }
        });

        window.addEventListener('unhandledrejection', function (event) {
            console.error('[silkit] unhandled promise', event.reason);
            if (bridge.isNative) {
                api.log('error', 'unhandled rejection: ' +
                    ((event.reason && (event.reason.stack || event.reason.message)) || String(event.reason)))
                    .catch(function () { /* ignore */ });
            }
        });
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', boot);
    } else {
        boot();
    }
})();
