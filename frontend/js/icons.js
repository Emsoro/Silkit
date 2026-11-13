(function () {
    'use strict';

    const PATHS = {
        plug: '<path d="M9 3v4M15 3v4"/><path d="M6 7h12v3a6 6 0 0 1-12 0V7Z"/><path d="M12 16v5"/>',
        terminal: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="m7 9 3 3-3 3M13 15h4"/>',
        folder: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7Z"/>',
        history: '<path d="M3 12a9 9 0 1 0 3-6.7"/><path d="M3 4v5h5"/><path d="M12 8v4l3 2"/>',
        pulse: '<path d="M3 12h4l2-5 3 10 2-5h7"/>',
        chevron: '<path d="m9 6 6 6-6 6"/>',
        search: '<circle cx="11" cy="11" r="6"/><path d="m20 20-4.2-4.2"/>',
        database: '<ellipse cx="12" cy="6" rx="7" ry="3"/><path d="M5 6v6c0 1.7 3.1 3 7 3s7-1.3 7-3V6"/><path d="M5 12v6c0 1.7 3.1 3 7 3s7-1.3 7-3v-6"/>',
        table: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M3 9h18M3 14h18M9 4v16M15 4v16"/>',
        grid: '<rect x="3" y="3" width="8" height="8" rx="1.5"/><rect x="13" y="3" width="8" height="8" rx="1.5"/><rect x="3" y="13" width="8" height="8" rx="1.5"/><rect x="13" y="13" width="8" height="8" rx="1.5"/>',
        message: '<path d="M21 12a8 8 0 0 1-8 8H7l-4 3V12a8 8 0 0 1 8-8h2a8 8 0 0 1 8 8Z"/><path d="M8 11h8M8 15h5"/>',
        plus: '<path d="M12 5v14M5 12h14"/>',
        play: '<path d="M8 5.5v13l11-6.5-11-6.5Z"/>',
        check: '<path d="m4.5 12.5 5 5 10-11"/>',
        palette: '<path d="M12 3a9 9 0 1 0 0 18c1.5 0 2-1 2-2 0-1.5 1-2 2.5-2H18a3 3 0 0 0 3-3 9 9 0 0 0-9-9Z"/><circle cx="7.5" cy="11" r="1"/><circle cx="10" cy="7.5" r="1"/><circle cx="15" cy="8" r="1"/>',
        close: '<path d="m6 6 12 12M18 6 6 18"/>',
        sparkle: '<path d="M12 3l1.8 5.2L19 10l-5.2 1.8L12 17l-1.8-5.2L5 10l5.2-1.8L12 3Z"/>'
    };

    function svg(name, size) {
        const body = PATHS[name] || '';
        return '<svg viewBox="0 0 24 24" width="' + (size || 16) + '" height="' + (size || 16) +
            '" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" ' +
            'stroke-linejoin="round" aria-hidden="true">' + body + '</svg>';
    }

    function mount(root) {
        (root || document).querySelectorAll('[data-icon]').forEach(function (el) {
            const name = el.getAttribute('data-icon');
            el.removeAttribute('data-icon');
            el.insertAdjacentHTML('afterbegin', svg(name));
        });
    }

    window.Icons = { svg: svg, mount: mount };
})();
