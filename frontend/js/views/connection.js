// 连接对话框：引擎选择（SQLite / MySQL / PostgreSQL）、文件选择、已保存连接、测试连接与打开/断开。
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

        // 引擎选择改为横排单选组，用这两个 helper 读写选中值。
        function engineValue() {
            const checked = dom.connEngine &&
                dom.connEngine.querySelector('input[name="connEngine"]:checked');
            return checked ? checked.value : 'sqlite';
        }

        function setEngineValue(value) {
            if (!dom.connEngine) return;
            const input = dom.connEngine.querySelector(
                'input[name="connEngine"][value="' + value + '"]');
            if (input) input.checked = true;
        }

        function init() {
            [
                'connDialog', 'connDialogClose', 'connTitle', 'connCloseX', 'connName', 'connEngine',
                'connFile', 'connBrowse', 'connNewFile', 'connReadOnly', 'connBusyTimeout',
                'connForeignKeys', 'connJournal', 'connError', 'connTestStatus', 'connList',
                'connTest', 'connSaveBtn', 'connConnectBtn', 'connDelete', 'sqliteFields',
                'connEmptyList', 'connAdvanced', 'serverFields', 'connHost', 'connPort',
                'connUser', 'connPassword', 'connDatabase', 'connSchemaField', 'connSchema',
                'connCharset', 'connSslMode', 'connServerReadOnly', 'connSavePassword',
                'connConnectTimeout', 'connInitSql'
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
            dom.connEngine.addEventListener('change', function () {
                updateEngineFields(true);
                if (dom.connTitle && !editingId) {
                    dom.connTitle.textContent =
                        '新建 ' + (ENGINE_LABELS[engineValue()] || 'SQLite') + ' 连接';
                }
                clearError();
            });

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

        const ENGINE_LABELS = { sqlite: 'SQLite', mysql: 'MySQL / MariaDB', postgres: 'PostgreSQL' };
        const DEFAULT_PORTS = { mysql: '3306', postgres: '5432' };
        const DEFAULT_USERS = { mysql: 'root', postgres: 'postgres' };

        function updateEngineFields(resetDefaults) {
            const engine = engineValue();
            const isSqlite = engine === 'sqlite';
            dom.sqliteFields.hidden = !isSqlite;
            dom.serverFields.hidden = isSqlite;
            dom.connAdvanced.hidden = !isSqlite;
            dom.connSchemaField.hidden = engine !== 'postgres';
            dom.connPort.placeholder = DEFAULT_PORTS[engine] || '3306';
            dom.connCharset.placeholder = engine === 'mysql' ? 'utf8mb4' : 'UTF8';
            if (resetDefaults) {
                dom.connPort.value = DEFAULT_PORTS[engine] || '';
                if (!dom.connHost.value) dom.connHost.value = '127.0.0.1';
                // 切换引擎时填入该引擎的默认用户名（用户手动输入过的值不覆盖）。
                const knownDefaults = Object.keys(DEFAULT_USERS).map(function (key) {
                    return DEFAULT_USERS[key];
                });
                const currentUser = dom.connUser.value.trim();
                if (!currentUser || knownDefaults.indexOf(currentUser) !== -1) {
                    dom.connUser.value = DEFAULT_USERS[engine] || '';
                }
            }
        }

        function fill(profile) {
            editingId = profile && profile.id ? profile.id : '';
            clearError();
            setStatus('', false);
            const p = profile || {};
            const options = p.options || {};
            const ssl = p.ssl || {};
            setEngineValue(ENGINE_LABELS[p.engine] ? p.engine : 'sqlite');
            dom.connName.value = p.name || '';
            dom.connFile.value = p.file || '';
            dom.connReadOnly.checked = !!p.readOnly;
            dom.connBusyTimeout.value = options.busyTimeoutMs != null ? options.busyTimeoutMs : 5000;
            dom.connForeignKeys.checked = options.foreignKeys !== false;
            dom.connJournal.value = options.journalMode || '';
            dom.connHost.value = p.host || '127.0.0.1';
            dom.connPort.value = p.port || DEFAULT_PORTS[engineValue()] || '';
            dom.connUser.value = p.user || DEFAULT_USERS[p.engine] || '';
            dom.connPassword.value = '';
            dom.connDatabase.value = p.database || '';
            dom.connSchema.value = p.schema || '';
            dom.connCharset.value = p.charset || '';
            dom.connSslMode.value = ssl.mode || 'prefer';
            dom.connServerReadOnly.checked = !!p.readOnly;
            dom.connSavePassword.checked = !!(p.secret && p.secret.save);
            dom.connConnectTimeout.value = options.connectTimeoutMs != null ? options.connectTimeoutMs : 8000;
            dom.connInitSql.value = options.initSql || '';
            updateEngineFields(false);
            if (dom.connTitle) dom.connTitle.textContent = editingId ? '编辑连接' :
                '新建 ' + (ENGINE_LABELS[engineValue()] || 'SQLite') + ' 连接';
            if (dom.connConnectBtn) dom.connConnectBtn.textContent = editingId ? '重新连接' : '连接';
            if (dom.connDelete) dom.connDelete.hidden = !editingId;
            renderProfiles();
        }

        function reset() {
            editingId = '';
            fill(null);
            dom.connName.value = '';
            dom.connFile.value = '';
            dom.connDatabase.value = '';
            dom.connSchema.value = '';
            dom.connPassword.value = '';
            dom.connSavePassword.checked = false;
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
            const engine = engineValue();
            let profile;

            if (engine === 'sqlite') {
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
                profile = {
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
            } else {
                const host = dom.connHost.value.trim() || '127.0.0.1';
                const port = dom.connPort.value.trim() || DEFAULT_PORTS[engine] || '';
                const user = dom.connUser.value.trim();
                if (!user) {
                    setError('请输入数据库用户名');
                    dom.connUser.focus();
                    return null;
                }
                let name = dom.connName.value.trim();
                if (!name) {
                    name = dom.connDatabase.value.trim() || (user + '@' + host);
                    dom.connName.value = name;
                }
                const timeout = Number(dom.connConnectTimeout.value);
                profile = {
                    name: name,
                    engine: engine,
                    host: host,
                    port: port,
                    user: user,
                    database: dom.connDatabase.value.trim(),
                    schema: engine === 'postgres' ? dom.connSchema.value.trim() : '',
                    charset: dom.connCharset.value.trim(),
                    readOnly: !!dom.connServerReadOnly.checked,
                    ssl: { mode: dom.connSslMode.value },
                    options: {
                        connectTimeoutMs: Number.isFinite(timeout) && timeout > 0 ? timeout : 8000,
                        initSql: dom.connInitSql.value.trim()
                    }
                };
            }

            if (editingId) profile.id = editingId;
            return {
                profile: profile,
                password: dom.connPassword.value,
                savePassword: !!dom.connSavePassword.checked
            };
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
            const data = collect();
            if (!data) return null;
            const profile = data.profile;
            try {
                const result = await api.connSave(profile, data.password, data.savePassword);
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
            const data = collect();
            if (!data) return;
            setBusy(true);
            setStatus('正在连接…', false);
            try {
                const result = await api.connTest(data.profile, data.password);
                const parts = [];
                if (result && result.serverVersion) {
                    parts.push((ENGINE_LABELS[data.profile.engine] || '') + ' ' + result.serverVersion);
                }
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
            const data = collect();
            if (!data) return;
            const profile = data.profile;
            setBusy(true);
            setStatus('正在打开数据库…', false);
            try {
                if (dom.connSaveBtn && !dom.connSaveBtn.hidden) {
                    // 已保存连接在「连接」时自动落盘（保持列表可复用）。
                    try {
                        const saved = await api.connSave(profile, data.password, data.savePassword);
                        if (saved && saved.id) {
                            profile.id = saved.id;
                            editingId = saved.id;
                        }
                    } catch (error) {
                        // 保存失败不阻塞连接。
                        console.warn('[silkit] 保存连接失败', error);
                    }
                }

                const opened = await api.connOpen(profile, data.password);
                const session = {
                    id: opened.sessionId,
                    profile: opened.profile || profile,
                    serverVersion: opened.serverVersion || '',
                    currentDb: opened.currentDb || (profile.engine === 'sqlite' ? 'main' : profile.database || ''),
                    capabilities: opened.capabilities || {},
                    engine: (opened.profile && opened.profile.engine) || profile.engine
                };
                closeDialog();
                toast('已连接 ' + (session.profile.name || ENGINE_LABELS[session.engine] || '数据库') +
                    (session.serverVersion ? ' · ' + session.serverVersion : ''));
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
