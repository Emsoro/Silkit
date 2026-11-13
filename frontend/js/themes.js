// 主题系统：5 套调色板 → <html data-theme data-appearance>。
// 主题文件只包含语义 token，组件 CSS 不感知具体主题。
(function () {
    'use strict';

    const api = window.SilkitApi;

    const THEMES = [
        {
            id: 'bloom', name: '晨樱', en: 'Bloom', appearance: 'light', default: true,
            swatches: ['#FFF7F9', '#FCEFF3', '#C2255C']
        },
        {
            id: 'noir', name: '夜樱', en: 'Noir', appearance: 'dark',
            swatches: ['#17121A', '#3A2230', '#F06595']
        },
        {
            id: 'lilac', name: '薰衣草', en: 'Lilac', appearance: 'light',
            swatches: ['#FAF7FF', '#F3EDFC', '#6D4FC4']
        },
        {
            id: 'mocha', name: '奶咖', en: 'Mocha', appearance: 'light',
            swatches: ['#FBF7F3', '#F6EFE8', '#96563D']
        },
        {
            id: 'mint', name: '薄荷', en: 'Mint', appearance: 'light',
            swatches: ['#F5FBF9', '#EAF6F2', '#237668']
        }
    ];

    const root = document.documentElement;
    const STORAGE = {
        theme: 'silkit.theme',
        appearance: 'silkit.appearance',
        lastLight: 'silkit.lastLight'
    };

    let currentId = 'bloom';
    let lastLight = 'bloom';
    let focusIndex = 0;
    let listEl = null;
    let systemQuery = null;
    let listeners = [];

    function byId(id) {
        for (let i = 0; i < THEMES.length; i += 1) {
            if (THEMES[i].id === id) return THEMES[i];
        }
        return null;
    }

    function indexOf(id) {
        for (let i = 0; i < THEMES.length; i += 1) {
            if (THEMES[i].id === id) return i;
        }
        return 0;
    }

    function current() {
        return byId(currentId) || THEMES[0];
    }

    function stored(key, fallback) {
        try {
            return localStorage.getItem(key) || fallback;
        } catch (error) {
            return fallback;
        }
    }

    function persistLocal(theme) {
        try {
            localStorage.setItem(STORAGE.theme, theme.id);
            localStorage.setItem(STORAGE.appearance, theme.appearance);
            if (theme.appearance === 'light') localStorage.setItem(STORAGE.lastLight, theme.id);
        } catch (error) { /* localStorage 不可用时忽略 */ }
    }

    function updateChrome(theme) {
        const label = document.getElementById('themeLabel');
        if (label) label.textContent = '主题·' + theme.name;

        const status = document.getElementById('statusTheme');
        if (status) status.textContent = theme.name;

        const trigger = document.getElementById('themeBtn');
        if (trigger) trigger.setAttribute('title', theme.name + ' ' + theme.en + ' · ' + (theme.appearance === 'dark' ? '深色' : '浅色'));
    }

    function renderSelection() {
        if (!listEl) return;
        const cards = listEl.querySelectorAll('.theme-card');
        for (let i = 0; i < cards.length; i += 1) {
            const selected = cards[i].getAttribute('data-theme-id') === currentId;
            cards[i].setAttribute('aria-selected', selected ? 'true' : 'false');
        }
    }

    function setFocus(index) {
        if (!listEl) return;
        const count = THEMES.length;
        focusIndex = ((index % count) + count) % count;

        const cards = listEl.querySelectorAll('.theme-card');
        for (let i = 0; i < cards.length; i += 1) {
            cards[i].classList.toggle('is-focus', i === focusIndex);
        }
        if (cards[focusIndex] && cards[focusIndex].scrollIntoView) {
            cards[focusIndex].scrollIntoView({ block: 'nearest' });
        }
    }

    function moveFocus(delta) {
        setFocus(focusIndex + delta);
    }

    function applyFocused() {
        return apply(THEMES[focusIndex].id);
    }

    function syncWindow(theme, persist) {
        if (!api) return;
        api.themeSet(theme.id, persist).catch(function (error) {
            console.warn('[silkit] theme.set 失败', error);
        });
    }

    function apply(id, options) {
        const opts = options || {};
        const theme = byId(id) || THEMES[0];
        const changed = theme.id !== currentId;

        currentId = theme.id;
        if (theme.appearance === 'light') lastLight = theme.id;

        root.dataset.theme = theme.id;
        root.dataset.appearance = theme.appearance;
        persistLocal(theme);
        updateChrome(theme);
        renderSelection();
        syncWindow(theme, opts.persist !== false);

        if (!opts.silent) {
            listeners.forEach(function (callback) {
                try {
                    callback(theme, changed);
                } catch (error) {
                    console.error('[silkit] theme listener', error);
                }
            });
            document.dispatchEvent(new CustomEvent('silkit:theme', { detail: theme }));
        }
        return theme;
    }

    /// Ctrl+Shift+L：当前浅色主题 ↔ 夜樱（记住上次的浅色主题）。
    function toggle() {
        const active = current();
        if (active.appearance === 'dark') {
            return apply(byId(lastLight) ? lastLight : 'bloom');
        }
        lastLight = active.id;
        return apply('noir');
    }

    function onChange(callback) {
        if (typeof callback === 'function') listeners.push(callback);
    }

    function buildPanel() {
        listEl = document.getElementById('themeList');
        if (!listEl) return;

        listEl.innerHTML = THEMES.map(function (theme, index) {
            const swatches = theme.swatches.map(function (color) {
                return '<i style="background:' + color + '"></i>';
            }).join('');
            const hint = theme.appearance === 'dark' ? '深色' :
                (theme.default ? '浅色 · 默认' : '浅色');
            return '<button class="theme-card" type="button" role="option" data-theme-id="' + theme.id + '"' +
                ' data-index="' + index + '" aria-selected="false">' +
                '<span class="theme-swatches" aria-hidden="true">' + swatches + '</span>' +
                '<span class="theme-card-main">' +
                '<span class="theme-card-name">' + theme.name +
                ' <span class="theme-card-id">' + theme.en + '</span></span>' +
                '<span class="theme-card-appearance">' + hint + '</span>' +
                '</span>' +
                '<span class="theme-check" data-icon="check"></span>' +
                '</button>';
        }).join('');

        if (window.Icons) window.Icons.mount(listEl);

        listEl.addEventListener('click', function (event) {
            const card = event.target.closest('.theme-card');
            if (!card) return;
            setFocus(Number(card.getAttribute('data-index')) || 0);
            apply(card.getAttribute('data-theme-id'));
        });

        listEl.addEventListener('pointermove', function (event) {
            const card = event.target.closest('.theme-card');
            if (!card) return;
            setFocus(Number(card.getAttribute('data-index')) || 0);
        });

        renderSelection();
        setFocus(indexOf(currentId));
    }

    function watchSystem() {
        if (!window.matchMedia) return;
        systemQuery = window.matchMedia('(prefers-color-scheme: dark)');
        const handler = function (event) {
            const active = current();
            if (event.matches && active.appearance !== 'dark') {
                lastLight = active.id;
                apply('noir', { persist: false });
            } else if (!event.matches && active.appearance === 'dark' && lastLight) {
                apply(lastLight, { persist: false });
            }
        };
        if (systemQuery.addEventListener) systemQuery.addEventListener('change', handler);
        else if (systemQuery.addListener) systemQuery.addListener(handler);
    }

    async function init() {
        lastLight = stored(STORAGE.lastLight, 'bloom');
        if (!byId(lastLight) || byId(lastLight).appearance !== 'light') lastLight = 'bloom';

        const initial = stored(STORAGE.theme, 'bloom');
        // 首帧已由 index.html 内联脚本设置；这里补齐 chrome、localStorage 与窗口底色。
        apply(initial, { persist: false, silent: true });
        buildPanel();

        let settings = null;
        try {
            const result = await api.settingsGet();
            settings = result && result.settings ? result.settings : null;
        } catch (error) {
            console.warn('[silkit] settings.get 失败', error);
        }

        if (settings && settings.ui) {
            const saved = settings.ui.theme;
            if (byId(saved) && saved !== currentId) {
                apply(saved, { persist: false, silent: true });
                setFocus(indexOf(saved));
            }
            if (settings.ui.followSystem) {
                watchSystem();
                if (systemQuery) {
                    const dark = systemQuery.matches;
                    if (dark && current().appearance !== 'dark') {
                        apply('noir', { persist: false, silent: true });
                    } else if (!dark && current().appearance === 'dark') {
                        apply(lastLight, { persist: false, silent: true });
                    }
                }
            }
        }

        return current();
    }

    window.SilkitThemes = {
        THEMES: THEMES,
        byId: byId,
        indexOf: indexOf,
        current: current,
        init: init,
        apply: apply,
        toggle: toggle,
        onChange: onChange,
        moveFocus: moveFocus,
        applyFocused: applyFocused,
        setFocus: function (index) { setFocus(index); },
        focusIndex: function () { return focusIndex; }
    };
})();
