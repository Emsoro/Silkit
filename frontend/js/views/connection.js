// SQLite 连接对话框：文件选择、已保存连接、测试连接、保存与打开/断开。
(function () {
    'use strict';

    const U = window.SilkitUtil;

    function create(ctx) {
        const api = ctx.api;
        const state = ctx.state;
        const toast = ctx.toast;

        const dom = {};
        let editingId = '';
        let busy = false;
        let profilesLoaded = false;

        function byId(id) { return document.getElementById(id); }

        function init() {
            [
                'connDialog', 'connDialogClose', 'connTitle', 'connCloseX', 'connName', 'connEngine',
                'connFile', 'connBrowse', 'connNewFile', 'connReadOnly', 'connBusyTimeout',
                'connForeignKeys', 'connJournal', 'connError', 'connTestStatus', 'connList',
                'connTest', 'connSaveBtn', 'connConnectBtn', 'connDelete', 'sqliteFields',
                'connEmptyList', 'connAdvanced'
            ].forEach(function (id) { dom[id] = byId(id); });

            if (!dom.connDialog) return;

            dom.connDialogClose.addEventListener('click', closeDialog);
            dom.connCloseX.addEventListener('click', closeDialog);
            dom.connDialog.addEventListener('click', function (event) {
                if (event.target === dom.connDialog) closeDialog();
            });
            dom.connDialog.addEventListener('keydown', function (event) {
                if (event.key === 'Escape') {
                    event.preventDefault();
                    closeDialog();
                }
            });

            dom.connBrowse.addEventListener('click', function () { pickFile(false); });
            dom.connNewFile.addEventListener('click', function () { pickFile(true); });
            dom.connTest.addEventListener('click', testConnection);
            dom.connSaveBtn.addEventListener('click', function () { saveProfile(true); });
            dom.connConnectBtn.addEventListener('click', connect);
            dom.connDelete.addEventListener('click', function () {
                if (editingId) deleteProfile(editingId);
            });
            dom.connFile.addEventListener('change', function () {
                if (!dom.connName.value.trim()) {
                    dom.connName.value = U.stripExtension(U.basename(dom.connFile.value)) || 'SQLite';
                }
                clearError();
            });
            dom.connName.addEventListener('input', clearError);

            dom.connList.addEventListener('click', function (event) {
                const del = event.target.closest('.conn-item-delete');
                if (del) {
                    event.stopPropagation();
                    deleteProfile(del.getAttribute('data-id'));
                    return;
                }
                const item = event.target.closest('.conn-item');
                if (!item) return;
                const profile = state.profiles.find(function (p) {
                    return p.id === item.getAttribute('data-id');
                });
                if (profile) fill(profile);
            });

            dom.connList.addEventListener('dblclick', function (event) {
                const item = event.target.closest('.conn-item');
                if (!item) return;
                const profile = state.profiles.find(function (p) {
                    return p.id === item.getAttribute('data-id');
                });
                if (profile) {
                    fill(profile);
                    connect();
                }
            });

            document.addEventListener('keydown', function (event) {
                if (event.key === 'Escape' && !dom.connDialog.hidden) closeDialog();
            });
        }

        async function loadProfiles() {
            try {
                const result = await api.connList();
                state.profiles = Array.isArray(result && result.connections) ? result.connections : [];
                profilesLoaded = true;
                renderProfiles();
                return state.profiles;
            } catch (error) {
                profilesLoaded = true;
                renderProfiles();
                if (ctx.onProfilesError) ctx.onProfilesError(error);
                return [];
            }
        }

        function renderProfiles() {
            if (!dom.connList) return;
            U.clear(dom.connList);
            if (dom.connEmptyList) dom.connEmptyList.hidden = state.profiles.length > 0;

            state.profiles.forEach(function (profile) {
                const item = document.createElement('div');
                item.className = 'conn-item' + (profile.id === editingId ? ' is-active' : '');
                item.setAttribute('data-id', profile.id);
                item.setAttribute('role', 'button');
                item.tabIndex = 0;

                const dot = U.el('span', 'conn-item-dot');
                dot.style.setProperty('--dot', profile.engine === 'sqlite' ? '#C2255C' : '#7C5CD6');
                const main = U.el('span', 'conn-item-main');
                main.appendChild(U.el('span', 'conn-item-name', profile.name || '未命名连接'));
                const detail = profile.engine === 'sqlite'
                    ? U.shortenPath(profile.file || '', 34)
                    : (profile.host || '') + (profile.port ? ':' + profile.port : '') +
                        (profile.database ? '/' + profile.database : '');
                const sub = U.el('span', 'conn-item-sub', detail || profile.engine || '');
                if (profile.file) sub.title = profile.file;
                main.appendChild(sub);

                const del = U.el('button', 'conn-item-delete');
                del.type = 'button';
                del.title = '删除此连接配置';
                del.setAttribute('data-id', profile.id);
                if (window.Icons) del.innerHTML = window.Icons.svg('trash', 12);
                else del.textContent = '×';

                item.appendChild(dot);
                item.appendChild(main);
                item.appendChild(del);
                dom.connList.appendChild(item);
            });
            if (dom.connDelete) dom.connDelete.hidden = !editingId;
        }

        function clearError() {
            if (!dom.connError) return;
            dom.connError.hidden = true;
            dom.connError.textContent = '';
        }

        function setError(message) {
            if (!dom.connError) return;
            dom.connError.hidden = false;
            dom.connError.textContent = message;
        }

        function setStatus(message, ok) {
            if (!dom.connTestStatus) return;
            dom.connTestStatus.hidden = !message;
            dom.connTestStatus.textContent = message || '';
            dom.connTestStatus.classList.toggle('is-ok', !!ok);
        }

        function fill(profile) {
            editingId = profile && profile.id ? profile.id : '';
            clearError();
            setStatus('', false);
            dom.connName.value = profile && profile.name || '';
            dom.connFile.value = profile && profile.file || '';
            dom.connReadOnly.checked = !!(profile && profile.readOnly);
            const options = (profile && profile.options) || {};
            dom.connBusyTimeout.value = options.busyTimeoutMs != null ? options.busyTimeoutMs : 5000;
            dom.connForeignKeys.checked = options.foreignKeys !== false;
            dom.connJournal.value = options.journalMode || '';
            if (dom.connTitle) dom.connTitle.textContent = editingId ? '编辑连接' : '新建 SQLite 连接';
            if (dom.connConnectBtn) dom.connConnectBtn.textContent = editingId ? '重新连接' : '连接';
            if (dom.connDelete) dom.connDelete.hidden = !editingId;
            renderProfiles();
        }

        function reset() {
            editingId = '';
            fill(null);
            dom.connName.value = '';
            dom.connFile.value = '';
        }

        function openDialog(profile) {
            if (!dom.connDialog) return;
            if (profile && profile.id) fill(profile);
            else reset();
            if (!profilesLoaded) loadProfiles();
            dom.connDialog.hidden = false;
            dom.connDialog.querySelector('.modal-dialog').classList.add('is-open');
            renderProfiles();
            setTimeout(function () {
                if (!dom.connName.value) dom.connName.focus();
                else dom.connConnectBtn.focus();
            }, 20);
        }

        function closeDialog() {
            if (!dom.connDialog) return;
            dom.connDialog.hidden = true;
            dom.connDialog.querySelector('.modal-dialog').classList.remove('is-open');
            setStatus('', false);
            if (ctx.onDialogClosed) ctx.onDialogClosed();
        }

        function setBusy(value) {
            busy = !!value;
            [dom.connTest, dom.connSaveBtn, dom.connConnectBtn, dom.connBrowse, dom.connNewFile]
                .forEach(function (button) {
                    if (!button) return;
                    button.disabled = busy;
                });
            if (dom.connConnectBtn) {
                dom.connConnectBtn.classList.toggle('is-busy', busy);
                dom.connConnectBtn.textContent = busy ? '连接中…' :
                    (editingId ? '重新连接' : '连接');
            }
        }

        function collect() {
            clearError();
            const file = dom.connFile.value.trim();
            if (!file) {
                setError('请选择 SQLite 数据库文件');
                dom.connFile.focus();
                return null;
            }
            let name = dom.connName.value.trim();
            if (!name) {
                name = U.stripExtension(U.basename(file)) || 'SQLite';
                dom.connName.value = name;
            }
            const busyTimeout = Number(dom.connBusyTimeout.value);
            const profile = {
                name: name,
                engine: 'sqlite',
                file: file,
                readOnly: !!dom.connReadOnly.checked,
                options: {
                    busyTimeoutMs: Number.isFinite(busyTimeout) && busyTimeout >= 0 ? busyTimeout : 5000,
                    foreignKeys: !!dom.connForeignKeys.checked,
                    journalMode: dom.connJournal.value || ''
                }
            };
            if (editingId) profile.id = editingId;
            return profile;
        }

        async function pickFile(save) {
            clearError();
            try {
                const result = await api.pickFile({
                    kind: 'sqlite',
                    save: !!save,
                    defaultName: save ? 'silkit.db' : '',
                    defaultPath: U.dirname(dom.connFile.value)
                });
                const path = result && result.path ? result.path : '';
                if (!path) return;
                dom.connFile.value = path;
                if (!dom.connName.value.trim()) {
                    dom.connName.value = U.stripExtension(U.basename(path)) || 'SQLite';
                }
            } catch (error) {
                setError('文件选择失败：' + (error.message || error));
            }
        }

        async function saveProfile(showToast) {
            const profile = collect();
            if (!profile) return null;
            try {
                const result = await api.connSave(profile, '', false);
                if (result && result.id) {
                    editingId = result.id;
                    profile.id = result.id;
                }
                await loadProfiles();
                fill(profile);
                if (showToast) toast('连接配置已保存');
                return profile;
            } catch (error) {
                setError('保存失败：' + (error.message || error));
                return null;
            }
        }

        async function testConnection() {
            if (busy) return;
            const profile = collect();
            if (!profile) return;
            setBusy(true);
            setStatus('正在连接…', false);
            try {
                const result = await api.connTest(profile);
                const parts = [];
                if (result && result.serverVersion) parts.push('SQLite ' + result.serverVersion);
                if (result && result.currentDb) parts.push(result.currentDb);
                if (result && typeof result.elapsedMs === 'number') parts.push(result.elapsedMs + ' ms');
                setStatus('连接成功 · ' + parts.join(' · '), true);
                toast('连接测试成功（' + (result.elapsedMs || 0) + ' ms）');
            } catch (error) {
                setStatus('', false);
                setError((error.message || '连接失败') +
                    (error.detail ? ' · ' + error.detail : ''));
            } finally {
                setBusy(false);
                renderProfiles();
            }
        }

        async function connect() {
            if (busy) return;
            const profile = collect();
            if (!profile) return;
            setBusy(true);
            setStatus('正在打开数据库…', false);
            try {
                if (dom.connSaveBtn && !dom.connSaveBtn.hidden) {
                    // 已保存连接在「连接」时自动落盘（保持列表可复用）。
                    try {
                        const saved = await api.connSave(profile, '', false);
                        if (saved && saved.id) {
                            profile.id = saved.id;
                            editingId = saved.id;
                        }
                    } catch (error) {
                        // 保存失败不阻塞连接。
                        console.warn('[silkit] 保存连接失败', error);
                    }
                }

                const opened = await api.connOpen(profile);
                const session = {
                    id: opened.sessionId,
                    profile: opened.profile || profile,
                    serverVersion: opened.serverVersion || '',
                    currentDb: opened.currentDb || 'main',
                    capabilities: opened.capabilities || {},
                    engine: (opened.profile && opened.profile.engine) || 'sqlite'
                };
                closeDialog();
                toast('已连接 ' + (session.profile.name || 'SQLite') +
                    (session.serverVersion ? ' · SQLite ' + session.serverVersion : ''));
                await loadProfiles();
                if (ctx.onOpen) ctx.onOpen(session);
            } catch (error) {
                const parts = [error.message || '连接失败'];
                if (error.detail) parts.push(error.detail);
                setError(parts.join(' · '));
                toast('连接失败：' + (error.message || '未知错误'), 'error');
            } finally {
                setBusy(false);
                if (dom.connConnectBtn) {
                    dom.connConnectBtn.textContent = editingId ? '重新连接' : '连接';
                }
                renderProfiles();
            }
        }

        async function deleteProfile(id) {
            const profile = state.profiles.find(function (p) { return p.id === id; });
            const name = profile ? profile.name : id;
            if (!window.confirm('删除连接「' + name + '」？不会删除数据库文件。')) return;
            try {
                await api.connDelete(id);
                if (editingId === id) editingId = '';
                await loadProfiles();
                fill(null);
                toast('已删除连接「' + name + '」');
            } catch (error) {
                toast('删除失败：' + (error.message || error), 'error');
            }
        }

        async function disconnect(sessionId) {
            const session = state.session(sessionId);
            if (!session) return;
            const name = (session.profile && session.profile.name) || sessionId;
            if (!window.confirm('断开连接「' + name + '」？')) return;
            try {
                await api.connClose(sessionId);
            } catch (error) {
                console.warn('[silkit] conn.close', error);
            }
            if (ctx.onClosed) ctx.onClosed(session);
            toast('已断开 ' + name);
        }

        function openForSession(session) {
            if (!session) return;
            openDialog(session.profile || null);
        }

        return {
            init: init,
            openDialog: openDialog,
            openForSession: openForSession,
            closeDialog: closeDialog,
            loadProfiles: loadProfiles,
            disconnect: disconnect,
            connect: connect,
            isBusy: function () { return busy; }
        };
    }

    window.SilkitConnections = { create: create };
})();
