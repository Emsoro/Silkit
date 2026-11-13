// 通用工具：DOM、格式化、剪贴板、SQL 标识符。
(function () {
    'use strict';

    function qs(selector, root) {
        return (root || document).querySelector(selector);
    }

    function qsa(selector, root) {
        return Array.prototype.slice.call((root || document).querySelectorAll(selector));
    }

    function el(tag, className, text) {
        const node = document.createElement(tag);
        if (className) node.className = className;
        if (text !== undefined && text !== null) node.textContent = String(text);
        return node;
    }

    function clear(node) {
        if (!node) return node;
        while (node.firstChild) node.removeChild(node.firstChild);
        return node;
    }

    function debounce(fn, wait) {
        let timer = 0;
        return function () {
            const args = arguments;
            const self = this;
            clearTimeout(timer);
            timer = setTimeout(function () { fn.apply(self, args); }, wait || 120);
        };
    }

    function rafThrottle(fn) {
        let queued = false;
        let lastArgs = null;
        return function () {
            lastArgs = arguments;
            if (queued) return;
            queued = true;
            requestAnimationFrame(function () {
                queued = false;
                fn.apply(null, lastArgs);
            });
        };
    }

    const numberFormat = (typeof Intl !== 'undefined')
        ? new Intl.NumberFormat('zh-CN')
        : null;

    function formatNumber(value) {
        const n = Number(value) || 0;
        return numberFormat ? numberFormat.format(n) : String(n);
    }

    function formatBytes(bytes) {
        const n = Number(bytes) || 0;
        if (n < 1024) return n + ' B';
        if (n < 1024 * 1024) return (n / 1024).toFixed(n < 10240 ? 1 : 0) + ' KB';
        if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + ' MB';
        return (n / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
    }

    function formatDuration(ms) {
        const n = Number(ms) || 0;
        if (n < 1000) return n + ' ms';
        if (n < 60000) return (n / 1000).toFixed(n < 10000 ? 2 : 1) + ' s';
        return Math.floor(n / 60000) + ' min ' + Math.round((n % 60000) / 1000) + ' s';
    }

    function basename(path) {
        if (!path) return '';
        const text = String(path).replace(/[\\/]+$/, '');
        const index = Math.max(text.lastIndexOf('\\'), text.lastIndexOf('/'));
        return index >= 0 ? text.slice(index + 1) : text;
    }

    function dirname(path) {
        if (!path) return '';
        const text = String(path).replace(/[\\/]+$/, '');
        const index = Math.max(text.lastIndexOf('\\'), text.lastIndexOf('/'));
        return index > 0 ? text.slice(0, index) : '';
    }

    function stripExtension(name) {
        const text = basename(name || '');
        const dot = text.lastIndexOf('.');
        return dot > 0 ? text.slice(0, dot) : text;
    }

    function shortenPath(path, maxLength) {
        const text = String(path || '');
        const max = maxLength || 42;
        if (text.length <= max) return text;
        const keep = Math.max(8, Math.floor((max - 3) / 2));
        return text.slice(0, keep) + '…' + text.slice(text.length - keep);
    }

    // SQLite/标准 SQL 的双引号标识符。
    function quoteIdentifier(name) {
        return '"' + String(name == null ? '' : name).replace(/"/g, '""') + '"';
    }

    function copyText(text) {
        const value = String(text == null ? '' : text);
        if (navigator.clipboard && window.isSecureContext !== false) {
            return navigator.clipboard.writeText(value).catch(function () {
                return fallbackCopy(value);
            });
        }
        return Promise.resolve(fallbackCopy(value));
    }

    function fallbackCopy(value) {
        const area = document.createElement('textarea');
        area.value = value;
        area.setAttribute('readonly', 'readonly');
        area.style.position = 'fixed';
        area.style.top = '-1000px';
        area.style.opacity = '0';
        document.body.appendChild(area);
        area.select();
        let ok = false;
        try {
            ok = document.execCommand('copy');
        } catch (error) {
            ok = false;
        }
        document.body.removeChild(area);
        if (!ok) throw new Error('复制失败');
        return true;
    }

    function lineColumnAt(text, offset) {
        const sql = String(text || '');
        const index = Math.max(0, Math.min(sql.length, Number(offset) || 0));
        const before = sql.slice(0, index);
        const line = before.split('\n').length;
        const lastBreak = before.lastIndexOf('\n');
        const column = index - lastBreak;
        return { line: line, column: column };
    }

    function truncateMiddle(text, max) {
        const value = String(text == null ? '' : text);
        const limit = max || 120;
        if (value.length <= limit) return value;
        const head = Math.ceil((limit - 1) / 2);
        const tail = Math.floor((limit - 1) / 2);
        return value.slice(0, head) + '…' + value.slice(value.length - tail);
    }

    function relativeTime(iso) {
        const then = Date.parse(iso || '');
        if (!then) return '';
        const delta = Date.now() - then;
        if (delta < 60000) return '刚刚';
        if (delta < 3600000) return Math.floor(delta / 60000) + ' 分钟前';
        if (delta < 86400000) return Math.floor(delta / 3600000) + ' 小时前';
        if (delta < 86400000 * 7) return Math.floor(delta / 86400000) + ' 天前';
        const date = new Date(then);
        return (date.getMonth() + 1) + '/' + date.getDate();
    }

    function clockTime(date) {
        const now = date || new Date();
        return [now.getHours(), now.getMinutes(), now.getSeconds()]
            .map(function (value) { return String(value).padStart(2, '0'); })
            .join(':');
    }

    function base64Bytes(text) {
        const value = String(text || '');
        if (!value) return 0;
        const padding = value.endsWith('==') ? 2 : (value.endsWith('=') ? 1 : 0);
        return Math.max(0, Math.floor(value.length * 3 / 4) - padding);
    }

    function hexPreview(base64, maxBytes) {
        const limit = maxBytes || 128;
        try {
            const binary = atob(base64);
            const bytes = [];
            const count = Math.min(binary.length, limit);
            for (let i = 0; i < count; i += 1) {
                bytes.push(('0' + binary.charCodeAt(i).toString(16)).slice(-2));
            }
            let out = bytes.join(' ');
            if (binary.length > limit) out += ' …';
            return out.toUpperCase();
        } catch (error) {
            return '';
        }
    }

    window.SilkitUtil = {
        qs: qs,
        qsa: qsa,
        el: el,
        clear: clear,
        debounce: debounce,
        rafThrottle: rafThrottle,
        formatNumber: formatNumber,
        formatBytes: formatBytes,
        formatDuration: formatDuration,
        basename: basename,
        dirname: dirname,
        stripExtension: stripExtension,
        shortenPath: shortenPath,
        quoteIdentifier: quoteIdentifier,
        copyText: copyText,
        lineColumnAt: lineColumnAt,
        truncateMiddle: truncateMiddle,
        relativeTime: relativeTime,
        clockTime: clockTime,
        base64Bytes: base64Bytes,
        hexPreview: hexPreview
    };
})();
