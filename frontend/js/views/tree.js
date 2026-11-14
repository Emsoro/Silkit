// 对象树：按需加载 meta.children，展开库/文件夹/表；双击表打开数据，右键菜单。
(function () {
    'use strict';

    const U = window.SilkitUtil;

    const FOLDER_LABELS = {
        tables: '表',
        views: '视图',
        indexes: '索引',
        triggers: '触发器',
        routines: '例程',
        events: '事件',
        sequences: '序列',
        databases: '数据库',
        columns: '列',
        foreign_keys: '外键'
    };

    function create(ctx) {
        const api = ctx.api;
        const state = ctx.state;
        const toast = ctx.toast;

        const dom = {};
        let activeNode = null;
        let profileList = [];

        function byId(id) { return document.getElementById(id); }

        function init() {
            dom.tree = byId('objectTree');
            dom.roots = byId('treeRoots');
            dom.empty = byId('treeEmpty');
            dom.filter = byId('treeFilter');
            dom.status = byId('treeStatus');
            dom.menu = byId('treeMenu');

            if (!dom.tree) return;

            dom.tree.addEventListener('click', onClick);
            dom.tree.addEventListener('dblclick', onDoubleClick);
            dom.tree.addEventListener('contextmenu', onContextMenu);

            if (dom.filter) {
                dom.filter.addEventListener('input', U.debounce(function () {
                    filter(dom.filter.value);
                }, 120));
            }

            U.qsa('[data-tree-action]', dom.tree).forEach(function (button) {
                button.addEventListener('click', function () {
                    if (button.getAttribute('data-tree-action') === 'new') {
                        ctx.connections.openDialog();
                    } else {
                        ctx.connections.openDialog();
                    }
                });
            });

            if (dom.menu) {
                dom.menu.addEventListener('click', function (event) {
                    const item = event.target.closest('.menu-item');
                    if (!item) return;
                    const node = dom.menu.__node;
                    hideMenu();
                    runAction(item.getAttribute('data-action'), node);
                });
            }

            document.addEventListener('pointerdown', function (event) {
                if (!dom.menu || dom.menu.hidden) return;
                if (!dom.menu.contains(event.target)) hideMenu();
            });
            document.addEventListener('keydown', function (event) {
                if (event.key === 'Escape') hideMenu();
            });

            state.on('active-session', highlightSession);
            updateStatus();
        }

        // ------------------------------------------------------------ 会话
        function sessionOf(node) {
            return node ? state.session(node.getAttribute('data-session')) : null;
        }

        function engineOf(session) {
            return (session && session.profile && session.profile.engine) ||
                (session && session.engine) || 'sqlite';
        }

        function supportsDatabase(session) {
            const engine = engineOf(session);
            return engine === 'mysql' || engine === 'postgres';
        }

        function dumpScopeOf(session, ref) {
            const engine = engineOf(session);
            if (engine === 'sqlite') return (ref && ref.database) || 'main';
            if (engine === 'postgres') return (ref && ref.schema) || 'public';
            if (ref && ref.database && ref.database !== 'main') return ref.database;
            return (session && session.currentDb) || '';
        }

        function engineLabelOf(engine) {
            if (engine === 'mysql') return 'MySQL';
            if (engine === 'postgres') return 'PostgreSQL';
            return 'SQLite';
        }

        function profileSummary(profile) {
            if (!profile) return '';
            if (profile.engine === 'sqlite') return profile.file || '';
            const target = (profile.user || '') + '@' + (profile.host || '') +
                (profile.port ? ':' + profile.port : '');
            return profile.database ? target + '/' + profile.database : target;
        }

        /// 未连接的已保存连接：占位节点，双击即可连接。
        function addProfileNode(profile) {
            if (!dom.roots || !profile) return null;
            let group = dom.roots.querySelector('.tree-group[data-profile="' + profile.id + '"]');
            if (group) return group;

            group = document.createElement('div');
            group.className = 'tree-group';
            group.setAttribute('data-profile', profile.id);

            const root = document.createElement('button');
            root.type = 'button';
            root.className = 'tree-node tree-root is-offline';
            root.setAttribute('data-profile', profile.id);
            root.setAttribute('data-kind', 'profile');
            root.setAttribute('data-name', profile.name || '未命名连接');
            root.setAttribute('data-has-children', '0');
            root.setAttribute('data-loaded', '1');
            root.style.setProperty('--depth', '0');
            root.title = (profileSummary(profile) || profile.name || '') + '\n双击连接';

            const caret = U.el('span', 'caret is-leaf');
            const dot = U.el('span', 'db-dot');
            dot.style.setProperty('--dot', 'var(--text-faint)');
            const label = U.el('span', 'node-label', profile.name || '未命名连接');
            const badge = U.el('span', 'node-badge', engineLabelOf(profile.engine));

            root.appendChild(caret);
            root.appendChild(dot);
            root.appendChild(label);
            root.appendChild(badge);
            group.appendChild(root);
            dom.roots.appendChild(group);
            return group;
        }

        /// 同步「已保存但未连接」的连接列表（连接成功后由会话节点接管）。
        function renderProfiles(profiles) {
            profileList = Array.isArray(profiles) ? profiles.slice() : [];
            if (!dom.roots) return;
            U.qsa('.tree-group[data-profile]', dom.roots).forEach(function (group) {
                group.remove();
            });
            const connected = {};
            state.sessions.forEach(function (session) {
                const id = session.profile && session.profile.id;
                if (id) connected[id] = true;
            });
            profileList.forEach(function (profile) {
                if (!connected[profile.id]) addProfileNode(profile);
            });
            updateStatus();
        }

        function addSession(session) {
            if (!dom.roots || !session) return;
            const profileId = session.profile && session.profile.id;
            if (profileId) {
                const placeholder = dom.roots.querySelector('.tree-group[data-profile="' + profileId + '"]');
                if (placeholder) placeholder.remove();
            }
            let group = dom.roots.querySelector('.tree-group[data-session="' + session.id + '"]');
            if (group) {
                highlightSession(state.activeSession());
                return group;
            }

            group = document.createElement('div');
            group.className = 'tree-group';
            group.setAttribute('data-session', session.id);

            const root = document.createElement('button');
            root.type = 'button';
            root.className = 'tree-node tree-root';
            root.setAttribute('data-session', session.id);
            root.setAttribute('data-kind', 'connection');
            root.setAttribute('data-name', (session.profile && session.profile.name) || 'SQLite');
            root.setAttribute('data-database', '');
            root.style.setProperty('--depth', '0');
            root.title = ((session.profile && session.profile.file) || '') + '\n双击编辑连接';

            const caret = U.el('span', 'caret');
            if (window.Icons) caret.innerHTML = window.Icons.svg('chevron', 11);
            const dot = U.el('span', 'db-dot');
            dot.style.setProperty('--dot', 'var(--accent)');
            const engine = (session.profile && session.profile.engine) || session.engine || 'sqlite';
            const engineLabel = engineLabelOf(engine);
            const label = U.el('span', 'node-label', (session.profile && session.profile.name) || engineLabel);
            const badge = U.el('span', 'node-badge', engineLabel);
            const spinner = U.el('span', 'node-spinner');
            spinner.hidden = true;

            root.appendChild(caret);
            root.appendChild(dot);
            root.appendChild(label);
            root.appendChild(badge);
            root.appendChild(spinner);

            const children = document.createElement('div');
            children.className = 'tree-children';
            children.hidden = true;

            group.appendChild(root);
            group.appendChild(children);
            dom.roots.appendChild(group);

            if (dom.empty) dom.empty.hidden = true;
            updateStatus();
            highlightSession(state.activeSession());

            // 打开连接后自动展开到表列表（main → tables）。
            expandNode(root, true).then(function () {
                const main = children.querySelector(':scope > .tree-node[data-kind="database"][data-name="main"]');
                if (!main) return null;
                return expandNode(main, true).then(function () {
                    const tables = main.nextElementSibling &&
                        main.nextElementSibling.querySelector(':scope > .tree-node[data-kind="folder"][data-name="tables"]');
                    if (!tables) return null;
                    return expandNode(tables, true);
                });
            }).catch(function (error) {
                console.warn('[silkit] 自动展开对象树失败', error);
            });

            return group;
        }

        function removeSession(sessionId) {
            if (!dom.roots) return;
            const session = state.session(sessionId);
            const group = dom.roots.querySelector('.tree-group[data-session="' + sessionId + '"]');
            if (group) group.remove();
            // 断开后回到「未连接」占位，仍留在左栏里方便再次连接。
            const profileId = session && session.profile && session.profile.id;
            if (profileId) {
                const profile = profileList.find(function (item) { return item.id === profileId; });
                if (profile) addProfileNode(profile);
            }
            updateStatus();
        }

        function refreshSession(sessionId) {
            const group = dom.roots.querySelector('.tree-group[data-session="' + sessionId + '"]');
            if (!group) return;
            const root = group.querySelector('.tree-node');
            const children = group.querySelector('.tree-children');
            if (!root || !children) return;
            children.hidden = true;
            U.clear(children);
            root.setAttribute('data-loaded', '0');
            const caret = root.querySelector('.caret');
            if (caret) caret.classList.remove('open');
            expandNode(root, true);
        }

        async function refreshActive() {
            const session = state.activeSession();
            if (!session) {
                ctx.connections.openDialog();
                return;
            }
            try {
                await api.metaRefresh(session.id);
            } catch (error) {
                console.warn('[silkit] meta.refresh', error);
            }
            refreshSession(session.id);
            toast('正在刷新对象树…');
        }

        function highlightSession(session) {
            if (!dom.roots) return;
            U.qsa('.tree-node.tree-root', dom.roots).forEach(function (node) {
                node.classList.toggle('is-active', !!session &&
                    node.getAttribute('data-session') === session.id);
            });
        }

        function updateStatus() {
            if (!dom.status) return;
            const active = state.sessions.size;
            const offline = dom.roots ? dom.roots.querySelectorAll('.tree-group[data-profile]').length : 0;
            const total = active + offline;
            const dot = byId('treeDot');
            if (dot) dot.classList.toggle('is-off', total === 0);
            if (!total) {
                dom.status.textContent = '尚未连接';
            } else if (active === 0) {
                dom.status.textContent = offline + ' 个已保存连接';
            } else if (active === 1 && offline === 0) {
                const session = state.activeSession();
                const profile = session && session.profile ? session.profile : {};
                dom.status.textContent = '1 个连接 · ' + (profile.name || 'SQLite');
            } else {
                dom.status.textContent = active + ' 个连接' +
                    (offline ? ' · ' + offline + ' 个未连接' : '');
            }
            if (dom.empty) dom.empty.hidden = total > 0;
        }

        // ------------------------------------------------------------ 节点
        function iconFor(kind, name) {
            if (kind === 'connection') return 'plug';
            if (kind === 'database') return 'database';
            if (kind === 'table') return 'table';
            if (kind === 'view') return 'eye';
            if (kind === 'routine') return 'terminal';
            if (kind === 'event') return 'bolt';
            if (kind === 'sequence') return 'pulse';
            if (kind === 'index') return 'key';
            if (kind === 'trigger') return 'bolt';
            if (kind === 'column') return 'column';
            if (kind === 'foreign_key') return 'link';
            if (kind === 'folder') {
                if (name === 'tables') return 'table';
                if (name === 'views') return 'eye';
                if (name === 'indexes') return 'key';
                if (name === 'triggers') return 'bolt';
                if (name === 'routines') return 'terminal';
                if (name === 'events') return 'bolt';
                if (name === 'sequences') return 'pulse';
                if (name === 'databases') return 'database';
                if (name === 'columns') return 'column';
                if (name === 'foreign_keys') return 'link';
            }
            return 'folder';
        }

        function labelFor(kind, name) {
            if (kind === 'folder' && FOLDER_LABELS[name]) return FOLDER_LABELS[name];
            return name;
        }

        function makeNode(session, item, depth, parentNode) {
            const kind = item.kind || 'folder';
            const name = item.name || '';
            const database = item.database || (parentNode && parentNode.getAttribute('data-database')) || 'main';
            let table = item.table || (parentNode && parentNode.getAttribute('data-table')) || '';
            if (!table && (kind === 'table' || kind === 'view')) table = name;
            const schema = item.schema ||
                (parentNode && parentNode.getAttribute('data-schema')) || '';
            const hasChildren = item.hasChildren != null ? !!item.hasChildren :
                (kind === 'connection' || kind === 'database' || kind === 'folder' ||
                    kind === 'table' || kind === 'view');

            const node = document.createElement('button');
            node.type = 'button';
            node.className = 'tree-node';
            node.setAttribute('data-session', session.id);
            node.setAttribute('data-kind', kind);
            node.setAttribute('data-name', name);
            node.setAttribute('data-database', database);
            node.setAttribute('data-schema', schema);
            node.setAttribute('data-table', table);
            node.setAttribute('data-has-children', hasChildren ? '1' : '0');
            if (!hasChildren) node.setAttribute('data-loaded', '1');
            node.style.setProperty('--depth', String(depth));
            if (item.column) node.setAttribute('data-column', item.column);
            if (item.refTable) node.setAttribute('data-ref-table', item.refTable);
            if (item.refColumn) node.setAttribute('data-ref-column', item.refColumn);

            const caret = U.el('span', 'caret' + (hasChildren ? '' : ' is-leaf'));
            if (hasChildren && window.Icons) caret.innerHTML = window.Icons.svg('chevron', 11);
            node.appendChild(caret);

            const icon = U.el('span', 'node-icon');
            if (window.Icons) icon.innerHTML = window.Icons.svg(iconFor(kind, name), 14);
            node.appendChild(icon);

            const label = U.el('span', 'node-label', labelFor(kind, name));
            node.appendChild(label);

            if (item.badge) {
                const mapped = kind === 'folder' && FOLDER_LABELS[name] === item.badge ? '' : item.badge;
                if (mapped) {
                    let badgeText = mapped;
                    if (kind === 'database') badgeText = U.basename(mapped) || mapped;
                    const badge = U.el('span', 'node-badge', badgeText);
                    badge.title = mapped;
                    node.appendChild(badge);
                }
            }
            if (item.column) node.title = name + ' · ' + (item.badge || '');
            if (kind === 'table' || kind === 'view') node.title = '双击打开数据';
            if (kind === 'connection') node.title = node.title || '';

            const spinner = U.el('span', 'node-spinner');
            spinner.hidden = true;
            node.appendChild(spinner);

            return { node: node, hasChildren: hasChildren };
        }

        function descriptorFor(node) {
            const kind = node.getAttribute('data-kind');
            const parent = {
                kind: kind,
                name: node.getAttribute('data-name') || '',
                database: node.getAttribute('data-database') || 'main',
                schema: node.getAttribute('data-schema') || '',
                table: node.getAttribute('data-table') || ''
            };
            return parent;
        }

        async function loadChildren(node) {
            if (node.getAttribute('data-loaded') === '1') return;
            const session = sessionOf(node);
            if (!session) return;
            if (node.getAttribute('data-loading') === '1') return;

            const group = node.closest('.tree-group');
            const container = node.nextElementSibling;
            if (!container || !container.classList.contains('tree-children')) return;
            const spinner = node.querySelector('.node-spinner');

            node.setAttribute('data-loading', '1');
            if (spinner) spinner.hidden = false;
            node.classList.add('is-loading');
            U.clear(container);

            try {
                const result = await api.metaChildren(session.id, descriptorFor(node));
                const nodes = result && Array.isArray(result.nodes) ? result.nodes : [];
                const depth = Number(node.style.getPropertyValue('--depth')) || 0;
                if (!nodes.length) {
                    const empty = U.el('div', 'tree-empty-node', '没有对象');
                    container.appendChild(empty);
                }
                nodes.forEach(function (item) {
                    const made = makeNode(session, item, depth + 1, node);
                    container.appendChild(made.node);
                    if (made.hasChildren) {
                        const childWrap = document.createElement('div');
                        childWrap.className = 'tree-children';
                        childWrap.hidden = true;
                        container.appendChild(childWrap);
                    }
                });
                node.setAttribute('data-loaded', '1');
                void group;
            } catch (error) {
                const message = (error && error.message) || '加载失败';
                const retry = document.createElement('button');
                retry.type = 'button';
                retry.className = 'tree-retry';
                retry.textContent = '加载失败：' + message + ' · 点击重试';
                retry.addEventListener('click', function (event) {
                    event.stopPropagation();
                    node.setAttribute('data-loaded', '0');
                    loadChildren(node);
                });
                container.appendChild(retry);
                node.setAttribute('data-loaded', '0');
                toast('加载对象失败：' + message, 'error');
            } finally {
                node.removeAttribute('data-loading');
                if (spinner) spinner.hidden = true;
                node.classList.remove('is-loading');
                if (dom.filter && dom.filter.value.trim()) filter(dom.filter.value);
            }
        }

        async function expandNode(node, force) {
            const container = node.nextElementSibling;
            if (!container || !container.classList.contains('tree-children')) return;
            if (!force && !container.hidden) {
                container.hidden = true;
                const caret = node.querySelector('.caret');
                if (caret) caret.classList.remove('open');
                return;
            }
            container.hidden = false;
            const caret = node.querySelector('.caret');
            if (caret) caret.classList.add('open');
            if (node.getAttribute('data-loaded') !== '1') await loadChildren(node);
        }

        function toggleNode(node) {
            if (node.getAttribute('data-has-children') === '0' &&
                node.getAttribute('data-kind') !== 'connection') return;
            const container = node.nextElementSibling;
            if (!container || !container.classList.contains('tree-children')) return;
            if (container.hidden) {
                expandNode(node, true);
            } else {
                container.hidden = true;
                const caret = node.querySelector('.caret');
                if (caret) caret.classList.remove('open');
            }
        }

        function selectNode(node) {
            if (!node) return;
            if (activeNode && activeNode !== node) activeNode.classList.remove('is-active');
            activeNode = node;
            node.classList.add('is-active');
            const session = sessionOf(node);
            if (session && state.activeSessionId !== session.id) state.setActiveSession(session.id);
        }

        function onClick(event) {
            const node = event.target.closest('.tree-node');
            if (!node || !dom.tree.contains(node)) return;
            selectNode(node);
            toggleNode(node);
        }

        function onDoubleClick(event) {
            const node = event.target.closest('.tree-node');
            if (!node) return;
            const kind = node.getAttribute('data-kind');
            if (kind === 'profile') {
                const profile = profileList.find(function (item) {
                    return item.id === node.getAttribute('data-profile');
                });
                if (profile && ctx.connections.connectProfile) ctx.connections.connectProfile(profile);
                return;
            }
            const session = sessionOf(node);
            if (!session) return;
            const name = node.getAttribute('data-name');
            const ref = descriptorFor(node);
            if (kind === 'table' || kind === 'view') {
                ctx.openTable(session, { kind: kind, name: name, database: ref.database,
                    schema: ref.schema });
            } else if (kind === 'connection') {
                ctx.connections.openForSession(session);
            } else if (kind === 'index' || kind === 'trigger' || kind === 'routine' ||
                kind === 'event' || kind === 'sequence') {
                ctx.showDdl(session.id, ref);
            }
        }

        // ------------------------------------------------------------ 右键菜单
        function showMenu(event, node) {
            if (!dom.menu) return;
            event.preventDefault();
            const nodeKind = node.getAttribute('data-kind');
            if (nodeKind === 'profile') {
                const profile = profileList.find(function (item) {
                    return item.id === node.getAttribute('data-profile');
                });
                if (!profile) return;
                selectNode(node);
                openMenu([
                    { action: 'connect-profile', label: '连接' },
                    { action: 'edit-profile', label: '编辑连接…' },
                    { separator: true },
                    { action: 'delete-profile', label: '删除连接', danger: true }
                ], node, event);
                return;
            }
            const session = sessionOf(node);
            if (!session) return;
            selectNode(node);
            const kind = node.getAttribute('data-kind');
            const name = node.getAttribute('data-name');
            const items = [];

            if (kind === 'connection') {
                items.push({ action: 'refresh', label: '刷新对象树' });
                items.push({ action: 'reconnect', label: '重新连接' });
                items.push({ action: 'dump-database', label: '转储数据库…' });
                if (supportsDatabase(session)) {
                    items.push({ action: 'create-database', label: '新建数据库…' });
                }
                items.push({ action: 'toggle-readonly',
                    label: session.readOnly ? '关闭只读模式' : '设为只读模式' });
                items.push({ action: 'edit', label: '编辑连接…' });
                items.push({ separator: true });
                items.push({ action: 'disconnect', label: '断开连接', danger: true });
            } else if (kind === 'table') {
                items.push({ action: 'open-data', label: '打开数据' });
                items.push({ action: 'detail', label: '查看结构与索引' });
                items.push({ action: 'ddl', label: '查看 DDL' });
                items.push({ action: 'generate-select', label: '生成 SELECT' });
                items.push({ separator: true });
                items.push({ action: 'export-table', label: '导出表数据…' });
                items.push({ action: 'import-csv', label: '导入 CSV…' });
                items.push({ action: 'dump-table', label: '转储表为 SQL…' });
                items.push({ separator: true });
                items.push({ action: 'design-table', label: '设计表…' });
                items.push({ action: 'truncate-table', label: '清空表…', danger: true });
                items.push({ action: 'drop-table', label: '删除表…', danger: true });
                items.push({ separator: true });
                items.push({ action: 'copy-name', label: '复制名称' });
            } else if (kind === 'view') {
                items.push({ action: 'open-data', label: '打开数据' });
                items.push({ action: 'detail', label: '查看结构' });
                items.push({ action: 'ddl', label: '查看 DDL' });
                items.push({ action: 'generate-select', label: '生成 SELECT' });
                items.push({ separator: true });
                items.push({ action: 'copy-name', label: '复制名称' });
            } else if (kind === 'index' || kind === 'trigger' || kind === 'routine' ||
                kind === 'event' || kind === 'sequence') {
                items.push({ action: 'ddl', label: '查看 DDL' });
                items.push({ action: 'copy-name', label: '复制名称' });
            } else if (kind === 'column' || kind === 'foreign_key') {
                items.push({ action: 'copy-name', label: '复制名称' });
            } else if (kind === 'database') {
                items.push({ action: 'create-table', label: '新建表…' });
                items.push({ action: 'dump-database', label: '转储数据库…' });
                items.push({ action: 'refresh-node', label: '刷新' });
                items.push({ action: 'copy-name', label: '复制名称' });
                if (supportsDatabase(session)) {
                    items.push({ separator: true });
                    items.push({ action: 'drop-database', label: '删除数据库…', danger: true });
                }
            } else if (kind === 'folder') {
                if (name === 'tables') {
                    items.push({ action: 'create-table', label: '新建表…' });
                } else if (name === 'databases' && supportsDatabase(session)) {
                    items.push({ action: 'create-database', label: '新建数据库…' });
                }
                items.push({ action: 'refresh-node', label: '刷新' });
                items.push({ action: 'copy-name', label: '复制名称' });
            }

            if (!items.length) return;
            openMenu(items, node, event);
        }

        function openMenu(items, node, event) {
            U.clear(dom.menu);
            items.forEach(function (item) {
                if (item.separator) {
                    dom.menu.appendChild(U.el('div', 'menu-sep'));
                    return;
                }
                const button = U.el('button', 'menu-item' + (item.danger ? ' is-danger' : ''), item.label);
                button.type = 'button';
                button.setAttribute('data-action', item.action);
                dom.menu.appendChild(button);
            });
            dom.menu.__node = node;
            dom.menu.hidden = false;

            const width = dom.menu.offsetWidth || 180;
            const height = dom.menu.offsetHeight || 120;
            const left = Math.min(event.clientX, window.innerWidth - width - 8);
            const top = Math.min(event.clientY, window.innerHeight - height - 8);
            dom.menu.style.left = Math.max(8, left) + 'px';
            dom.menu.style.top = Math.max(8, top) + 'px';
        }

        function onContextMenu(event) {
            const node = event.target.closest('.tree-node');
            if (!node) return;
            showMenu(event, node);
        }

        function hideMenu() {
            if (dom.menu) dom.menu.hidden = true;
        }

        function runAction(action, node) {
            if (!node) return;
            if (node.getAttribute('data-kind') === 'profile') {
                const profile = profileList.find(function (item) {
                    return item.id === node.getAttribute('data-profile');
                });
                if (!profile) return;
                if (action === 'connect-profile' && ctx.connections.connectProfile) {
                    ctx.connections.connectProfile(profile);
                } else if (action === 'edit-profile') {
                    ctx.connections.openDialog(profile);
                } else if (action === 'delete-profile' && ctx.connections.deleteProfile) {
                    ctx.connections.deleteProfile(profile.id);
                }
                return;
            }
            const session = sessionOf(node);
            if (!session) return;
            const kind = node.getAttribute('data-kind');
            const name = node.getAttribute('data-name');
            const ref = descriptorFor(node);

            switch (action) {
                case 'export-table':
                    if (ctx.io) {
                        ctx.io.openExport({
                            ref: { kind: 'table', name: name, database: ref.database, schema: ref.schema },
                            label: '表 ' + name,
                            format: 'csv',
                            pathHint: name
                        });
                    }
                    break;
                case 'import-csv':
                    if (ctx.io) {
                        ctx.io.openImport({
                            ref: { kind: 'table', name: name, database: ref.database, schema: ref.schema },
                            label: '导入 CSV 到 ' + name
                        });
                    }
                    break;
                case 'dump-table':
                    if (ctx.io) {
                        ctx.io.openDump({
                            label: '转储表 ' + name,
                            scope: dumpScopeOf(session, ref),
                            tables: [name]
                        });
                    }
                    break;
                case 'dump-database':
                    if (ctx.io) {
                        ctx.io.openDump({
                            label: '转储数据库 ' + (ref.database && ref.database !== 'main'
                                ? ref.database : (session.currentDb || name || '')),
                            scope: dumpScopeOf(session, ref)
                        });
                    }
                    break;
                case 'design-table':
                    if (ctx.designer) ctx.designer.openTableDesigner(session, ref);
                    break;
                case 'create-table':
                    if (ctx.designer) {
                        ctx.designer.openTableDesigner(session, {
                            kind: 'folder',
                            database: ref.database,
                            schema: ref.schema
                        });
                    }
                    break;
                case 'drop-table':
                    if (ctx.designer) ctx.designer.openDropTable(session, ref);
                    break;
                case 'truncate-table':
                    if (ctx.designer) ctx.designer.openTruncateTable(session, ref);
                    break;
                case 'create-database':
                    if (ctx.designer) ctx.designer.openCreateDatabase(session, ref);
                    break;
                case 'drop-database':
                    if (ctx.designer) {
                        ctx.designer.openDropDatabase(session, {
                            database: ref.database && ref.database !== 'main'
                                ? ref.database : name
                        });
                    }
                    break;
                case 'refresh':
                case 'refresh-node':
                    if (kind === 'connection') refreshSession(session.id);
                    else {
                        node.setAttribute('data-loaded', '0');
                        const container = node.nextElementSibling;
                        if (container) U.clear(container);
                        expandNode(node, true);
                    }
                    break;
                case 'reconnect':
                    api.connReconnect(session.id).then(function (result) {
                        session.serverVersion = result.serverVersion || session.serverVersion;
                        session.currentDb = result.currentDb || session.currentDb;
                        session.capabilities = result.capabilities || session.capabilities;
                        session.readOnly = !!(session.profile && session.profile.readOnly);
                        state.emit('active-session', session);
                        refreshSession(session.id);
                        toast('已重新连接');
                    }).catch(function (error) {
                        toast('重连失败：' + (error.message || error), 'error');
                    });
                    break;
                case 'toggle-readonly':
                    api.connSetReadonly(session.id, !session.readOnly).then(function (result) {
                        session.readOnly = !!result.readOnly;
                        state.emit('active-session', session);
                        toast(session.readOnly ? '已设为只读，写语句将被拒绝' : '已恢复可写');
                    }).catch(function (error) {
                        toast('切换失败：' + (error.message || error), 'error');
                    });
                    break;
                case 'edit':
                    ctx.connections.openForSession(session);
                    break;
                case 'disconnect':
                    ctx.connections.disconnect(session.id);
                    break;
                case 'open-data':
                    ctx.openTable(session, ref);
                    break;
                case 'detail':
                    ctx.showDetail(session.id, ref);
                    break;
                case 'ddl':
                    ctx.showDdl(session.id, ref);
                    break;
                case 'generate-select':
                    ctx.openQuery(session, 'SELECT * FROM ' + U.quoteIdentifier(name) + ' LIMIT 500;');
                    break;
                case 'copy-name':
                    U.copyText(name).then(function () {
                        toast('已复制「' + name + '」');
                    }).catch(function (error) {
                        toast('复制失败：' + (error.message || error), 'error');
                    });
                    break;
                default:
                    break;
            }
        }

        // ------------------------------------------------------------ 过滤
        function matches(node, query) {
            const label = node.querySelector('.node-label');
            const text = label ? label.textContent.toLowerCase() : '';
            const name = node.getAttribute('data-name') || '';
            return text.indexOf(query) >= 0 || name.toLowerCase().indexOf(query) >= 0;
        }

        function filterNode(node, query) {
            let selfMatch = matches(node, query);
            let childMatch = false;
            const container = node.nextElementSibling;
            if (container && container.classList.contains('tree-children')) {
                U.qsa(':scope > .tree-node', container).forEach(function (child) {
                    if (filterNode(child, query)) childMatch = true;
                });
                if (childMatch) {
                    container.hidden = false;
                    const caret = node.querySelector('.caret');
                    if (caret) caret.classList.add('open');
                }
            }
            const visible = selfMatch || childMatch;
            node.hidden = !visible;
            return visible;
        }

        function filter(raw) {
            const query = String(raw || '').trim().toLowerCase();
            if (!dom.roots) return;
            const groups = U.qsa('.tree-group', dom.roots);
            groups.forEach(function (group) {
                const root = group.querySelector(':scope > .tree-node');
                if (!root) return;
                const visible = filterNode(root, query);
                group.hidden = !visible;
            });
            if (!query) {
                U.qsa('.tree-node, .tree-group, .tree-children', dom.roots).forEach(function (node) {
                    node.hidden = false;
                });
                U.qsa('.tree-children', dom.roots).forEach(function (node) {
                    const parent = node.previousElementSibling;
                    node.hidden = !(parent && parent.querySelector('.caret.open'));
                });
                U.qsa('.tree-group', dom.roots).forEach(function (group) { group.hidden = false; });
            }
        }

        return {
            init: init,
            addSession: addSession,
            removeSession: removeSession,
            renderProfiles: renderProfiles,
            refreshSession: refreshSession,
            refreshActive: refreshActive,
            updateStatus: updateStatus,
            filter: filter,
            hideMenu: hideMenu
        };
    }

    window.SilkitTree = { create: create };
})();
