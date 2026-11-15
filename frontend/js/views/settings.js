// M8 设置面板：通用 / 编辑器 / 查询 / 数据网格 / 导出 / 关于。
//
//   const view = window.SilkitSettings.create(app);
//   view.open();
//
// 所有控件用 data-path（如 "editor.fontSize" / "query.safeMode"）与 settings.json 对应；
// 保存时只提交面板中存在的字段，后端递归合并。
(function () {
    'use strict';

    const U = window.SilkitUtil;
    const api = window.SilkitApi;
    const I = window.SilkitI18n;

    function t(key, vars) {
        return I ? I.t(key, vars) : key;
    }

    function pathGet(object, path) {
        let value = object;
        const parts = String(path || '').split('.');
        for (const part of parts) {
            if (!value || typeof value !== 'object' || !(part in value)) return undefined;
            value = value[part];
        }
        return value;
    }

    function create(ctx) {
        const dialog = U.qs('#settingsDialog');
        if (!dialog) return { open: function () {} };

        const stateEl = U.qs('#settingsState', dialog);
        const tabs = U.qsa('.settings-tab', dialog);
        const panes = U.qsa('.settings-pane', dialog);
        const controls = U.qsa('[data-path]', dialog);
        let dirty = false;
        let busy = false;
        let pathsLoaded = false;

        function selectTab(name) {
            tabs.forEach(function (tab) {
                tab.classList.toggle('active', tab.dataset.tab === name);
            });
            panes.forEach(function (pane) {
                pane.hidden = pane.dataset.pane !== name;
            });
            if (name === 'about') loadPaths();
        }

        function setDirty(value) {
            dirty = !!value;
            if (stateEl) stateEl.textContent = dirty ? t('settings.unsaved') : '';
            if (stateEl) stateEl.dataset.dirty = dirty ? '1' : '0';
        }

        function fill(settings) {
            controls.forEach(function (control) {
                const value = pathGet(settings, control.dataset.path);
                if (control.type === 'checkbox') {
                    control.checked = !!value;
                } else if (value === undefined || value === null) {
                    control.value = '';
                } else {
                    control.value = value;
                }
            });
            setDirty(false);
        }

        function collect() {
            const patch = {};
            controls.forEach(function (control) {
                const parts = control.dataset.path.split('.');
                let target = patch;
                for (let i = 0; i < parts.length - 1; i += 1) {
                    const key = parts[i];
                    if (!target[key] || typeof target[key] !== 'object') target[key] = {};
                    target = target[key];
                }
                let value;
                if (control.type === 'checkbox') {
                    value = control.checked;
                } else if (control.type === 'number') {
                    const number = Number(control.value);
                    value = Number.isFinite(number) ? Math.max(0, Math.trunc(number)) : 0;
                } else {
                    value = control.value;
                }
                target[parts[parts.length - 1]] = value;
            });
            return patch;
        }

        function loadPaths() {
            if (pathsLoaded) return;
            pathsLoaded = true;
            Promise.all([api.appInfo(), api.appPaths()])
                .then(function (results) {
                    const info = results[0] || {};
                    const paths = results[1] || {};
                    U.qs('#aboutVersion', dialog).textContent = info.version || '—';
                    U.qs('#aboutDataDir', dialog).textContent = paths.dataDir || '—';
                    U.qs('#aboutLogsDir', dialog).textContent = paths.logsDir || '—';
                    U.qs('#aboutSettings', dialog).textContent = paths.settingsFile || '—';
                    U.qs('#aboutLogFile', dialog).textContent = paths.logFile || '—';
                })
                .catch(function () {
                    pathsLoaded = false;
                });
        }

        function open() {
            api.settingsGet().then(function (result) {
                fill((result && result.settings) || {});
                selectTab('general');
                dialog.hidden = false;
                const first = tabs[0];
                if (first) first.focus();
            }).catch(function (error) {
                console.error('[silkit] load settings', error);
            });
        }

        function close() {
            if (busy) return;
            const wasOpen = !dialog.hidden;
            dialog.hidden = true;
            if (wasOpen) {
                const trigger = U.qs('#settingsBtn');
                if (trigger) trigger.focus();
            }
        }

        function save() {
            if (busy) return;
            busy = true;
            const patch = collect();
            api.settingsSet(patch).then(function (result) {
                const settings = (result && result.settings) || {};
                if (ctx && typeof ctx.applySettings === 'function') ctx.applySettings(settings);
                setDirty(false);
                busy = false;
                if (stateEl) stateEl.textContent = t('settings.saved');
                setTimeout(function () {
                    if (!dirty) dialog.hidden = true;
                }, 450);
            }).catch(function (error) {
                busy = false;
                if (stateEl) stateEl.textContent = (error && error.message) || '保存失败';
            });
        }

        async function reset() {
            if (busy) return;
            if (!(await U.confirm({
                title: '恢复默认设置',
                message: t('settings.resetConfirm'),
                okText: '恢复默认',
                danger: true
            }))) return;
            busy = true;
            api.settingsReset().then(function (result) {
                const settings = (result && result.settings) || {};
                fill(settings);
                if (ctx && typeof ctx.applySettings === 'function') ctx.applySettings(settings);
                busy = false;
                if (stateEl) stateEl.textContent = t('settings.resetDone');
                pathsLoaded = false;
                loadPaths();
            }).catch(function (error) {
                busy = false;
                if (stateEl) stateEl.textContent = (error && error.message) || '重置失败';
            });
        }

        tabs.forEach(function (tab) {
            tab.addEventListener('click', function () {
                selectTab(tab.dataset.tab);
            });
        });
        dialog.addEventListener('input', function () { setDirty(true); });
        dialog.addEventListener('change', function () { setDirty(true); });
        dialog.addEventListener('click', function (event) {
            if (event.target === dialog) close();
        });
        U.qs('#settingsClose', dialog).addEventListener('click', close);
        U.qs('#settingsCancel', dialog).addEventListener('click', close);
        U.qs('#settingsSave', dialog).addEventListener('click', save);
        U.qs('#settingsReset', dialog).addEventListener('click', reset);
        U.qsa('[data-open-path]', dialog).forEach(function (button) {
            button.addEventListener('click', function () {
                api.appOpenPath(button.dataset.openPath).then(function (result) {
                    if (result && result.ok === false) {
                        if (stateEl) stateEl.textContent = t('settings.openFailed');
                    }
                }).catch(function () {
                    if (stateEl) stateEl.textContent = t('settings.openFailed');
                });
            });
        });

        if (I) {
            I.onChange(function () {
                dialog.querySelectorAll('[data-i18n]').forEach(function (el) {
                    el.textContent = t(el.getAttribute('data-i18n'));
                });
                if (stateEl && stateEl.dataset.dirty === '1') stateEl.textContent = t('settings.unsaved');
            });
        }

        document.addEventListener('keydown', function (event) {
            if (event.key === 'Escape' && !dialog.hidden && !busy) {
                event.stopPropagation();
                close();
            }
        });

        return {
            open: open,
            close: close,
            isOpen: function () { return !dialog.hidden; }
        };
    }

    window.SilkitSettings = { create: create };
})();
