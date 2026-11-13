// 桥接层：优先使用 TauriCPP 注入的原生桥（window.__tauricpp__），
// 浏览器里打开 index.html 调试时自动回退到 js/mock/mock.js。
// 原生桥可能在脚本执行后才注入，因此每次调用都惰性解析。
(function () {
    'use strict';

    function native() {
        const bridge = window.__tauricpp__;
        return bridge && typeof bridge.invoke === 'function' ? bridge : null;
    }

    function impl() {
        return native() || window.MockBridge || null;
    }

    const bridge = {
        get isNative() {
            return !!native();
        },

        invoke(command, args) {
            const target = impl();
            if (!target) return Promise.reject(new Error('bridge unavailable'));
            try {
                return Promise.resolve(target.invoke(command, args || {}));
            } catch (error) {
                return Promise.reject(error);
            }
        },

        listen(event, callback) {
            const target = impl();
            if (!target || typeof target.listen !== 'function') return function () {};
            return target.listen(event, callback);
        },

        emit(event, data) {
            const target = impl();
            if (target && typeof target.emit === 'function') target.emit(event, data);
        }
    };

    // 前端异常统一写入日志文件（原生环境）；mock 环境走 console。
    window.addEventListener('error', function (event) {
        const detail = (event && event.message ? event.message : String(event)) +
            (event && event.error && event.error.stack ? '\n' + event.error.stack : '');
        if (!bridge.isNative) {
            console.error('[silkit]', detail);
            return;
        }
        bridge.invoke('ipc.call', {
            cmd: 'app.log',
            reqId: 'err-' + Date.now().toString(36),
            args: { level: 'error', message: detail }
        }).catch(function () { /* ignore */ });
    });

    window.SilkitBridge = bridge;
})();
