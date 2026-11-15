# Silkit

**简体中文** · [English](README.en.md)

跨库数据库工作台（MySQL / MariaDB / PostgreSQL / SQLite），Windows 桌面端。
C++20 + Win32 + WebView2，前端为原生 HTML/CSS/JS，无 Node 构建步骤。

> 当前版本：**v1.0.4**（2026-11-15）。进度：**M0–M9 全部交付**：连接 → 对象树 → CodeMirror SQL 编辑器 → 流式结果（多结果/EXPLAIN）→ 表格数据编辑 → 表设计器 → 历史/片段 → CSV 导入、五种格式流式导出、SQL 转储 → 设置面板、安全模式与日志脱敏 → 发布打包；v1.0.2 起新建查询自动继承所选库、查询页可切连接/库并智能补全表与列；v1.0.3 起补全按整条 SQL 的 `FROM` 表限定列，并在候选右侧显示所属路径；v1.0.4 起关键字候选不再显示右侧标签。
> 完整计划与偏差记录见 [CODEPLAN.md](CODEPLAN.md)。

## 版本说明

| 版本 | 日期 | 类型 | 说明 |
|---|---|---|---|
| **v1.0.4** | 2026-11-15 | 展示优化 | 补全候选右侧只保留来源路径：列 `库.表`、表库名；关键字不再显示 `keyword` 标签，`render` 对关键字只渲染左侧名称。`node tools/test_hint.js` 扩充到 46 项。详见 [CHANGELOG.md](CHANGELOG.md)。 |
| **v1.0.3** | 2026-11-15 | 补全修复 | 补全改为按分号切分光标所在的整条 SQL 解析表引用（含写在光标之后的 `FROM`），列候选限定于语句中真实出现的表；候选右侧统一显示所属路径（列 `employees.departments`、表 `employees`、关键字 `keyword`），去掉名称后的 `(table)` / `(view)` 标记。`node tools/test_hint.js` 扩充到 45 项（其中关键字右侧标签已在 v1.0.4 移除）。详见 [CHANGELOG.md](CHANGELOG.md)。 |
| **v1.0.2** | 2026-11-15 | 体验增强 | 新建查询自动继承对象树选中的连接与库并 `USE`；查询页新增「连接 / 库」下拉，切换后自动切库，标签各自记住上下文；SQL 补全升级为关键字 + 表/视图 + 列名，支持 `表名.` / `别名.` 与按库隔离的元数据缓存；修复启动即弹查询页且关掉又弹一个的问题，改为可彻底关闭的无标签空态。新增 `node tools/test_hint.js`（31 项）与 `node tools/test_tabs.js`（5 项）无浏览器自测。详见 [CHANGELOG.md](CHANGELOG.md)。 |
| v1.0.1 | 2026-11-15 | 维护版 | 修复 MariaDB Connector/C 3.4.5 在无 TLS 的 MySQL 协议服务端（如 SQLTiny）上即使选择 `disable` 仍强制 SSL 的连接失败；连接失败错误条与 toast 新增「复制信息」一键复制完整诊断；编译标准升级到 **C++20**；补充真实库 E2E 矩阵与一键执行入口；界面移除顶栏 `M8` 徽章与状态栏 M8 连接状态，右下角版本号改用徽章小胶囊样式；所有确认弹窗统一为主题化界面，不再弹出系统原生白框。详见 [CHANGELOG.md](CHANGELOG.md)。 |
| v1.0.0 | 2026-11-14 | 首个正式版 | M0–M9 全部交付，Windows 单 exe 便携发布（`Silkit-1.0.0-win-x64.zip`），内置 MySQL / MariaDB / PostgreSQL / SQLite 驱动。 |

> 版本号以 `CMakeLists.txt` 的 `project(Silkit VERSION ...)` 为唯一来源；`vcpkg.json`、`src/app/version.h`、状态栏、Mock 后端与本文档保持一致，exe 版本资源由 CMake 自动生成。执行 `python tools/check_version.py` 校验一致性。

## 功能概览

| 能力 | 说明 |
|---|---|
| 连接 | MySQL / MariaDB、PostgreSQL、SQLite；测试连接、DPAPI 保存密码、颜色与分组、只读模式 |
| 对象浏览 | 库 / 模式 / 表 / 视图 / 函数 / 存储过程 / 触发器；懒加载、搜索、右键菜单、DDL 查看 |
| 查询 | 多标签编辑器、语法高亮、**关键字 + 表/视图 + 列名智能补全**、执行当前/选中/全部、多结果集、EXPLAIN、取消、历史与片段 |
| 查询上下文 | 新建查询继承对象树选中的连接与库并自动 `USE`；编辑器工具条「连接 / 库」下拉、切换自动切库、每个标签各自记住所属库 |
| 工作区空态 | 启动与关闭最后一个标签后不自动新建查询页，显示「无标签」引导卡片；关闭状态随会话恢复保留 |
| 结果网格 | 虚拟滚动、列宽调整、排序筛选、TSV/CSV/JSON/INSERT 复制、长文本与 BLOB 查看、导出 |
| 表数据 | 分页、服务端筛选排序、内联增删改、事务保存与回滚、SQL 预览、无主键只读保护 |
| 表设计 | 列 / 索引 / 外键 / 表选项、SQL 预览与令牌执行、影响行数与风险提示 |
| 导入导出 | CSV 导入向导、CSV/JSON/SQL/Markdown/HTML 流式导出、整库/整表 SQL 转储、`.sql` 文件执行 |
| 设置与安全 | 五套主题、中英文界面、安全模式危险语句确认、日志脱敏、崩溃日志、会话恢复 |

### 支持矩阵

| 引擎 | 连接 | 查询 | 表数据编辑 | 表设计器 | 导入导出/转储 | 验收状态 |
|---|---|---|---|---|---|---|
| SQLite 3.45+ | ✅ | ✅ | ✅ | ✅ | ✅ | 自动化测试全绿 |
| MySQL 8.x | ✅ | ✅ | ✅ | ✅ | ✅ | 对照 8.4.13 实机验收 |
| MariaDB 10.x / 11.x | ✅ | ✅ | ✅ | ✅ | ✅ | 共用 libmariadb 驱动，未逐版本部署 |
| PostgreSQL 13–18 | ✅ | ✅ | ✅ | ✅ | ✅ | 对照 18.7 实机验收 |

> 平台：Windows 10 1809+ / Windows 11（x64）。依赖系统 Microsoft Edge WebView2 Runtime。

### 查询上下文与智能补全（v1.0.2 起，v1.0.3 / v1.0.4 增强）

- **自动继承选中库**：在左侧对象树选中数据库、表或视图后，点顶栏「新建查询」/ 标签栏 `+` / `Ctrl+N`，新标签直接绑定该连接与该库；若会话当前库不同，自动执行 `USE`（MySQL）/ `conn.set_database`（PG 重连、SQLite 限已 ATTACH 库），并在编辑器标题显示当前库。
- **连接 / 库下拉**：编辑器工具条新增两个下拉，可随时改本标签使用的连接与库。库切换后立即生效（清空该会话的补全缓存并刷新对象树）；切换回标签或按 `Ctrl+Enter` 执行时，如果会话被别的标签切走了库，会自动切回本标签记录的库。标签的 `database / schema / table` 随会话恢复一起持久化。
- **智能补全**：候选包含 SQL 关键字、当前库的表与视图，以及 `FROM / JOIN / UPDATE / INTO` 中表的列；输入 `表名.` 或 `别名.` 时**只**提示该表的列（列名带类型说明，插入位置在点号之后），解析不到表就返回空，不会串进其它表的列；`FROM / JOIN / UPDATE / INTO` 后的 `库名.`（PG 为 `schema.`）视为库限定符，提示该库的表名。候选按「常用关键字 → 表名 → 列名 → 冷门关键字」排序，函数与引擎专属子句不会挤掉表名/列名。元数据按 `会话 | 引擎 | 库 | schema` 缓存，切库或 DDL 变更后自动失效；首次触发按需异步拉取，不阻塞输入。**v1.0.3**：表引用按分号切分光标所在的整条语句解析（含写在光标之后的 `FROM`，并忽略注释），因此 `SELECT departments.dept_no, d| FROM departments` 输入 `d` 也会提示 `dept_name`；列候选只来自语句中真实出现的表。候选右侧只显示来源——列 `employees.departments`（PG 为 `schema.表`）、表 `employees`，名称不再带 `(table)` / `(view)` 后缀；**v1.0.4**：关键字不再显示 `keyword` 标签，右侧留空。
- **自测**：`node tools/test_hint.js`（无浏览器，46 项）覆盖表名/视图、点号列补全、别名、库隔离、缓存失效、schema 透传，以及「FROM 在光标之后」「多语句/注释不串列」「右侧来源路径与自定义 render」等回归。

### 无标签空态（v1.0.2 修复）

启动、连接成功、关闭最后一个标签都不再自动补建查询页：主区域显示「没有打开的标签」卡片，提供「新建查询」「打开 SQL」按钮与 `Ctrl+N` 提示，编辑器、结果区与分割条一并隐藏。空标签集随会话恢复持久化，重启后仍保持关闭，只有显式新建查询、打开 `.sql` 或拖放文件才创建标签。`node tools/test_tabs.js`（5 项）锁定该行为，并覆盖关闭非激活标签不误切换。

## 发布与打包（M9 于 v1.0.0 交付）

- **版本信息**：`CMakeLists.txt`、`vcpkg.json`、`src/app/version.h`、状态栏与 Mock 后端统一为 `1.0.4`（首发版 `1.0.0`）；exe 版本资源由 CMake 依据 `PROJECT_VERSION` 生成，`tools/check_version.py` 校验各处一致。
- **图标与窗口**：exe 内嵌多分辨率 `app_icon.ico`，标题栏/任务栏使用大小两种尺寸；窗口标题为「Silkit — 跨库数据库工作台」。
- **关于页**：显示版本、数据目录、日志目录、配置文件与当前日志，可直接打开数据/日志目录。
- **许可声明**：[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) 列出 TauriCPP、nlohmann/json、WebView2 SDK、SQLite、libmariadb、libpq 与 CodeMirror 的许可证与源码地址。
- **发布包**：`tools/package.ps1` 构建 Release 并生成 `dist/Silkit-1.0.4-win-x64.zip`（exe + 中英文 README + LICENSE + 第三方声明），解压即用；压缩包名中的版本号从 `CMakeLists.txt` 读取，无需手工同步。
- **验收**：CTest 全绿、对比度检查通过、版本一致性检查通过；干净 Windows 11 虚拟机冒烟待人工执行（见下方清单）。

## 发布验收清单

- [ ] 干净 Windows 11（有 WebView2）：解压 `dist/Silkit-1.0.4-win-x64.zip`，双击 `Silkit.exe` 能启动并显示晨樱主题。
- [ ] 无 WebView2 的 Windows 10：启动时出现中文安装引导，安装运行时后可用。
- [ ] 三库回归：SQLite 文件库、MySQL 8.x、PostgreSQL 13+ 各完成连接 → 对象树 → 查询 → 编辑表数据 → 设计表 → 导出/导入。
- [ ] 重启后标签、SQL、侧栏布局恢复；五套主题切换无白闪。
- [ ] 缩放 200% 与多显示器拖动下文字与网格清晰。
- [ ] 只读连接拒绝写操作；开启安全模式后执行 `DROP TABLE` 需二次确认。
- [ ] 选中其它库的表后「新建查询」，编辑器标题与「库」下拉显示该库，`SELECT DATABASE()` 返回它。
- [ ] 查询页切换「库」后执行查询命中新库；切到另一标签再切回会自动恢复各自的库。
- [ ] 输入 `表名.` 弹出该表列（含类型），输入 `别名.` 也正确；切库后候选随新库刷新。
- [ ] `node tools/test_hint.js` 46 项全通过。
- [ ] `%APPDATA%\Silkit\logs` 与 `settings.json` 中无明文密码。

## M0 已交付

- 可构建的单文件 `Silkit.exe`（静态 CRT，仅依赖系统 WebView2 运行时）
- 异步 IPC：`ipc.call` 立即返回，worker 线程池通过 `ipc.event` 回传 `progress / result / error / cancelled`
- UI 外壳：顶栏、对象树、SQL 编辑器、结果网格、消息面板、状态栏、分栏拖拽
- 五套主题 + 顶栏切换器 + 快捷键，主题与窗口底色持久化，启动无白闪
- 配置写入 `%APPDATA%\Silkit\settings.json`（原子替换），日志按天写入 `%APPDATA%\Silkit\logs\`
- 自检入口：顶栏「自检」跑一次 3 步异步任务，可在执行中取消
- `tools/contrast_check.py`：校验五套主题的关键配色对比度（WCAG 2.1）

## M1 已交付

- **SQLite 驱动**：连接参数（只读 / busy timeout / 外键 / `journal_mode` 白名单 / `init_sql`）、参数化执行、流式分批、跨线程取消、错误位置定位
- **Session 模型**：一个连接一条专用线程 + 任务队列；重连保持 `sessionId`、事务状态、运行时只读切换、`session.closed` 事件
- **IPC 命令**：`conn.*`（test/open/close/reconnect/ping/stats/set_database/set_readonly）、`meta.*`（children/detail/ddl/search/refresh）、`query.*`（execute/cancel/explain）
- **查询执行**：`sql_splitter` 拆分多语句并按序执行、`statement`/`batch` 流式事件、错误定位、`continueOnError`、`maxRows` 截断
- **前端**：连接对话框与文件选择、对象树懒加载/右键菜单（重连、只读）、查询标签、虚拟滚动结果网格、TSV 复制、长文本/BLOB 单元格查看器、最近查询
- **安全**：DPAPI 保存密码、`conn.list` 脱敏、驱动层拦截写语句、标识符引用与值参数化
- **测试**：`silkit_smoke`（纯逻辑/内存库/会话）与 `sqlite_driver`（驱动健壮性与会话状态回归），均由 CTest 运行

## M8 设置、安全与打磨已交付

- **设置面板**：顶栏齿轮打开，六个分页（通用 / 编辑器 / 查询 / 数据网格 / 导出 / 关于）；语言、动效、侧栏宽度、恢复标签、危险操作确认、编辑器字号/缩进/换行/行号/补全、安全模式、最大行数/分批大小/历史上限、分页行数/行高/NULL 文本、导出编码/分隔符/NULL 字面量均可配置，保存后立即生效。
- **配置恢复**：每次成功写盘前保留 `settings.json.bak`；主配置损坏时自动从备份恢复并重写；「恢复默认」一键重置。
- **安全模式**：默认开启；`query.execute` 与 `query.execute_file` 在拆分语句后统一检查，命中 `DROP / TRUNCATE / ALTER / GRANT / REVOKE / ATTACH / DETACH / VACUUM` 或无 `WHERE` 的 `UPDATE / DELETE` 时返回 `confirm_required` 与一次性令牌，前端列出语句并要求确认，语句变化令牌即失效。只读连接仍在驱动层拒绝写操作。
- **日志与崩溃**：日志写入统一脱敏（password/passwd/pwd/secret/token/api_key 等）；未处理异常写 `%APPDATA%\Silkit\logs\crash-*.log`（版本/时间/异常码/地址/线程），关于页可直接打开数据目录与日志目录。
- **会话恢复**：恢复上次激活的标签、编辑器滚动位置、侧栏宽度与折叠状态；侧栏宽度支持拖拽与键盘调整。
- **主题**：`contrast_check.py` 对五套主题 125 项配对全部通过（含弹窗、网格、编辑器 token）。
- **键盘与焦点**：`Esc` 关闭设置，弹窗打开时快捷键不误触发，`:focus-visible` 焦点环覆盖按钮/输入/分隔条。
- **测试**：CTest `m8` 覆盖日志脱敏、设置备份/损坏恢复/重置、危险语句识别与令牌稳定性。

## M7 导入 / 导出 / 转储已交付

- **CSV 基础**：`io/csv` 解析 RFC4180（引号转义、跨行字段、CRLF/LF/CR），写出按需加引号；自动探测 `,` `;` `Tab` `|` 分隔符。
- **流式导出**：`io/exporter` 支持 CSV / JSON / SQL / Markdown / HTML，内部 64KB 缓冲并按编码边界切块；SQL 每 100 行合成多值 INSERT，JSON 数字不带引号、NULL → `null`、二进制转十六进制。
- **导出向导**：结果集工具栏与对象树右键复用同一对话框；格式/编码/分隔符/表头/NULL 字面量可配置，进度实时回传，可取消；先写 `<path>.part`，成功后再替换正式文件，取消或失败自动清理。
- **CSV 导入向导**：选择文件后自动探测编码与分隔符并预览前 20 行；逐列编辑目标列名（留空跳过）、可选首行为表头/空字段转 NULL/自动建表；批量 INSERT 包在事务中，失败回滚并用逐行回放定位到具体数据行与文件行号。
- **SQL 文件执行**：工具栏「执行 SQL 文件」逐条执行并回传 `statement` 事件与进度，支持遇错继续与取消。
- **SQL 转储**：整库或指定表，结构（`CREATE TABLE`/索引/外键）、数据、`DROP TABLE IF EXISTS` 可分别开关；同样使用 `.part` 与进度事件。
- **编码**：UTF-8、UTF-8 BOM、UTF-16LE（含 BOM 与 emoji 代理对）、GBK 导入导出；无 BOM 时按 UTF-8 合法性自动判断，非法回退 GBK。
- **测试**：`test_io`（CTest `io`）覆盖 CSV 多行/引号/探测、UTF-8↔GBK/UTF-16LE 往返、五种导出格式片段、100 万行流式导出、10 万行导入、错误回滚行号定位、取消回滚，以及 SQLite 整库转储到空库恢复。

## M6 表设计器与 DDL 已交付

- **三引擎 DDL 生成**：`db/ddl_builder` 生成 SQLite / MySQL / PostgreSQL 的 CREATE / ALTER / DROP / TRUNCATE，覆盖列、主键、自增、默认值、注释、索引、外键与 MySQL 表选项。
- **表设计器**：对象树「设计表」或工具栏「设计」打开；列/索引/外键可增删与上下移动，选项、类型建议、SQL 预览与警告分面板展示；脏状态阻止误关闭。
- **统一预览与令牌**：`ddl.preview` 生成 SQL 与 FNV-1a 令牌，`ddl.apply` 校验令牌后执行；DDL 变更即失效，执行失败返回失败语句、已执行条数与是否回滚。
- **预览信息完整**：SQL 预览附带影响行数（CREATE `0`、DROP/TRUNCATE 实际计数、ALTER 显示“未知”）与风险警告（丢列、缩短长度、改类型、收紧 NOT NULL、唯一索引可能创建失败、MySQL 不可回滚）。
- **标识符校验**：新表名/列名/索引名/外键名按引擎限制长度并校验 UTF-8，禁止控制字符、引号与分号。
- **危险操作确认**：删除表 / 清空表 / 删除数据库需输入对象名；MySQL 显示不可回滚警告，PG/SQLite 使用事务或保存点。
- **SQLite 限制提示**：修改列定义、主键或外键时明确提示「不支持，请重建表」，不静默降级。
- **库与对象 DDL**：MySQL/PG 新建/删除数据库；视图/函数/触发器在详情面板查看 DDL 并保存为 `.sql`。
- **测试**：CTest `ddl_builder` 覆盖三引擎 SQL 生成、令牌失效与参数校验；`sqlite_fixture` 校验 `data/silkit_test.db` 夹具的元数据、数据与只读保护，并在临时副本上执行文件库 DDL；`integration_db` 在 SQLite 内存库、真实 MySQL 8.4.13 与 PostgreSQL 18.0.7 上完成建表→改表→重命名→清空→删除→建库/删库全链路。夹具可用 `python tools/make_test_db.py` 重建。

## M5 表格数据编辑已交付

- **数据网格**：双击对象树表/视图直接打开可编辑网格（`grid.js` 虚拟滚动）；分页、排序、搜索、筛选（操作符 + 值 + AND 组合）。
- **行标识与只读**：自动探测主键 / 唯一非空索引 / SQLite `rowid`；无安全行标识的表自动只读并提示原因。
- **内联编辑状态机**：新增行、删除行、单元格编辑；脏单元格/新增/删除行高亮；工具栏 新增 / 删除 / 保存 / 撤销 / 刷新 / 精确计数。
- **事务保存**：`table.apply` 在事务中执行 INSERT/UPDATE/DELETE（连接已事务中则用 SAVEPOINT），任一语句失败整体回滚并定位到出错行（`clientIndex`）。保存前展示 SQL 预览。
- **单元格查看器**：双击/右键打开长文本、JSON、图片、HEX 查看与另存。
- **复制/导出**：区域选择、TSV 复制、SQL/INSERT 复制格式。
- **三引擎对照真实服务器验收**：MySQL 8.4.13 与 PostgreSQL 18.0.7 的 CRUD、唯一键冲突回滚、精确计数均通过；`test_table_ops`（SQLite）覆盖分引擎构建与回滚。

## M4 查询工作区已交付

- **编辑器**：CodeMirror 5（vendor 内嵌），SQL 关键字/字符串/注释高亮随主题 token；每标签独立 Doc（undo 历史、光标、滚动独立）。
- **自动补全**：`Ctrl+Space` 或输入 2 字符后触发；表/列来自 `meta.children` 元数据缓存 + 引擎关键字。
- **执行范围**：`Ctrl+Enter` 全部/选中文本，`Ctrl+Shift+Enter` 光标所在语句；工具栏可切换默认范围。
- **结果面板**：多结果集子标签；CSV/JSON 一键导出；执行计划（EXPLAIN）子标签，三引擎方言；消息面板错误定位。
- **文件**：打开/保存/另存 `.sql`（Ctrl+S），支持拖放打开；后端 `app.read_file`/`app.save_file` 限文本扩展名 ≤16MB。
- **历史/片段**：历史 5000 条 + 片段 200 条，搜索、点击回填/插入，双写 localStorage 与 settings.json。
- **格式化**：轻量 SQL 格式化（Shift+Alt+F 或工具栏），保留字大写、主子句换行、括号缩进、列表逗号换行。

## M2 / M3 已交付并对照真实服务器验收

- **MySQL / MariaDB**：`libmariadb` 静态库；连接（SSL/字符集/超时/只读）、`mysql_use_result` 流式、prepared statement 参数化、`KILL QUERY` 取消、`information_schema` 元数据与 `SHOW CREATE` DDL、错误码友好提示。
- **PostgreSQL**：`libpq` 静态库；`PQconnectdbParams`（`search_path`、`bytea_output=hex`）、`PQsetSingleRowMode` 单行流式、`PQsendQueryParams` 参数化、`PQcancel` 取消、`pg_catalog` 元数据、`pg_get_*def` DDL 与目录简化重建、SQLSTATE 错误映射。
- **共用**：`db/engine_dialect.*` 汇总类型映射 / 错误提示 / 元数据 SQL；`ipc/commands_meta_engine.*` 为 `meta.*` 做引擎分派，SQLite 路径保持不变；`conn.set_database` 按引擎分别用 `USE` / 重连 / ATTACH。
- **前端**：连接对话框启用引擎选择，MySQL/PG 表单（主机/端口/用户/密码/库/schema/字符集/SSL/超时/初始 SQL）、密码 DPAPI 保存、测试连接；对象树按引擎显示徽章并传递 `schema`。
- **测试**：`engine_dialect`（类型/错误/SQL 生成，无需服务器）、`integration_db`（按 `SILKIT_MYSQL_*` / `SILKIT_PG_*` 环境变量连接真实服务器，未设置时跳过）、`e2e_real_db`（14 项 × 双引擎真实库 E2E 矩阵；可选 `SSL-01~03` 覆盖不声明 `CLIENT_SSL` 的 MySQL 协议服务端，用 `--mysql-nossl-host` 启用）。一键执行 `python tools/e2e_runner.py`，详见 `CODEPLAN.md` §12.4 / §12.9。
- **连接失败诊断**：错误条与错误提示最右侧均有「复制信息」，复制完整错误（错误码 / SQLSTATE / 详情 / 提示）与连接上下文，不含密码。MySQL 报 `2026 TLS/SSL error` 时按提示把「字符集 / SSL」设为 `disable`（或保持 `prefer` 自动降级）。

> 验收记录：MySQL 8.4.13 与 PostgreSQL 18.7 均完成连接、`SELECT 1`、对象树（库/文件夹/表/列/详情/搜索/DDL）、取消与错误提示的端到端验证（含本机部署的 MySQL 8.4.13 与 PostgreSQL 18；此前状态栏把 PG 版本显示为 `18.0.7`，已修复为 `18.7`）。
> 已知缺口：PG 表 DDL 为目录简化重建，与 `pg_dump --schema-only` 有差距；MariaDB 11、PostgreSQL 13–16 未逐版本部署（同驱动路径）；类型矩阵与性能验收（5000 表 / 1000 表元数据）未实测。

## 构建

### 环境要求

| 依赖 | 版本 / 说明 |
|---|---|
| Windows | 10 1809+ / 11 |
| Visual Studio | 2022（Desktop development with C++，需支持 `/std:c++20`） |
| C++ 标准 | C++20（`CMAKE_CXX_STANDARD 20`、`CMAKE_CXX_EXTENSIONS OFF`） |
| CMake | 3.21+ |
| vcpkg | 安装在 `C:\vcpkg`，或设置 `VCPKG_ROOT` |
| Python | 3.8+（前端资源打包、对比度检查） |
| WebView2 Runtime | Win11 自带；Win10 需安装 Evergreen Runtime |

准备 vcpkg 依赖（只需一次）：

```powershell
C:\vcpkg\vcpkg.exe install webview2:x64-windows-static sqlite3:x64-windows-static `
    libmariadb:x64-windows-static libpq:x64-windows-static --classic
```

> `vcpkg.json` 的 `builtin-baseline` 已固定到当前 vcpkg 仓库中存在的提交（`ef7dbf94…`）。`build.ps1` 会自动沿用已有 `build/` 的 vcpkg 模式；全新构建目录若已按 classic 模式装好依赖，也会直接复用（`-DVCPKG_MANIFEST_MODE=OFF`），无需手工切换。若需强制 manifest 模式，请先 `-Clean` 并删除 `build/` 中的 `CMakeCache.txt`。

### 编译

```powershell
# Release，嵌入前端资源，并运行 CTest
.\build.ps1

# 清理后重建
.\build.ps1 -Clean

# 跳过测试 / 调试前端（不嵌入资源，从 ./frontend 读取）
.\build.ps1 -SkipTests
.\build.ps1 -DevMode
```

产物：`build\Release\Silkit.exe`。

### 发布包

```powershell
# 构建 Release、运行 CTest 并生成 dist/Silkit-1.0.4-win-x64.zip
.\tools\package.ps1

# 跳过测试 / 只打包已构建的产物
.\tools\package.ps1 -SkipTests
.\tools\package.ps1 -SkipBuild
```

版本一致性检查：

```powershell
python tools\check_version.py
```

### 对比度检查

```powershell
python tools\contrast_check.py      # 只显示失败项与汇总
python tools\contrast_check.py -v   # 显示全部 125 项配对
node tools\test_hint.js             # 智能补全无浏览器自测（46 项）
node tools\test_tabs.js             # 标签空态回归自测（5 项）
```

## 快捷键

| 快捷键 | 功能 |
|---|---|
| `Ctrl+N` | 新建查询（继承对象树选中的连接与库） |
| `Ctrl+Space` | 手动触发关键字 / 表 / 列补全 |
| `Ctrl+Shift+T` | 打开 / 关闭主题面板 |
| `Ctrl+Shift+L` | 当前浅色主题 ↔ 夜樱（记住上次浅色主题） |
| `1`–`5` | 主题面板打开时快速选择第 N 套主题 |
| `↑` / `↓` / `Enter` | 主题面板键盘导航 / 应用 |
| `Esc` | 关闭主题面板；无面板时取消正在执行的自检 |
| `Ctrl+B` | 收起 / 展开侧栏 |
| `F12` | WebView2 DevTools（Debug 构建） |

## 主题

| id | 名称 | 明暗 | 主色 |
|---|---|---|---|
| `bloom` | 晨樱 Bloom | 浅（默认） | `#C2255C` |
| `noir` | 夜樱 Noir | 深 | `#F06595` |
| `lilac` | 薰衣草 Lilac | 浅 | `#6D4FC4` |
| `mocha` | 奶咖 Mocha | 浅 | `#96563D` |
| `mint` | 薄荷 Mint | 浅 | `#237668` |

主题文件只包含 CSS 变量；组件样式统一引用语义 token（`--bg-0`、`--text`、`--accent`…）。
新增主题 = 复制 `frontend/css/themes/bloom.css` 改一套变量 + 在 `js/themes.js` 与 `src/ipc/commands.cpp` 注册。

## 目录结构

```
Silkit/
├─ src/
│  ├─ main.cpp                 # WinMain、单实例、窗口/路由/配置装配
│  ├─ app/version.h            # 名称与版本
│  ├─ core/                    # 路径、日志、设置、profiles、DPAPI 密码
│  ├─ db/                      # 驱动抽象、SQL 分割/引用/类型、Session/Manager
│  │  └─ drivers/sqlite_driver.cpp
│  ├─ ipc/                     # AsyncRouter（worker 池）+ app/conn/meta/query 命令
│  └─ util/                    # base64 等
├─ frontend/
│  ├─ index.html               # 外壳 + 首帧防闪脚本
│  ├─ css/                     # tokens / base / layout / workbench + themes/*.css
│  └─ js/                      # bridge / api / state / themes / views / components / mock
├─ tests/                      # silkit_smoke + sqlite_driver（CTest）
├─ tools/contrast_check.py     # WCAG 对比度检查
├─ TauriCPP/                   # Win32 + WebView2 框架（内置）
├─ CODEPLAN.md                 # 完整产品/技术/里程碑计划
├─ CMakeLists.txt
└─ build.ps1
```

## 架构要点

- **UI 线程不阻塞**：`Bridge::HandleInvoke` 在 UI 线程同步执行，因此 `ipc.call` 只做参数校验与入队，耗时任务全部交给 `AsyncRouter` 的 worker。
- **事件回传**：worker 通过 `Bridge::Emit("ipc.event", …)` 投递到 JS 队列，由 `Window::ExecuteJsFromQueue` 在 UI 线程执行。
- **防白闪**：`index.html` 内联脚本先读 `localStorage` 设置 `data-theme`，再加载 CSS；C++ 侧在窗口创建时把类背景刷与 WebView2 `DefaultBackgroundColor` 设为主题底色。
- **配置**：`settings.set` 递归合并后原子写盘；前端只提交 patch。
- **驱动隔离**：`db/Connection` 抽象 + `DriverRegistry`，UI 只认统一模型；M2/M3 只需新增驱动。
- **会话亲和**：每个连接一条 Session 线程，驱动对象不跨线程使用；`Cancel()` 单独设计，可跨线程调用。
- **流式结果**：`query.execute` 按语句发 `statement` / `batch`，首批携带列元数据，达到 `maxRows` 后标记 `truncated`。

## 已知限制（v1.0.4）

- **平台**：目前仅 Windows 桌面端，依赖系统 WebView2 运行时；macOS / Linux 尚未支持。
- **导入 / 导出 / 转储**：CSV 导入需整文件读入（上限 256MB）；SQL 文件执行整文件读入（上限 512MB）后逐条执行，尚未按块流式拆分；SQL 转储只覆盖表结构与数据，视图/函数/触发器定义请从详情面板另存 `.sql`。
- **转储兼容性**：PostgreSQL 的结构由 `LoadDdlDefinition` 重建，与 `pg_dump` 输出存在差异；跨引擎恢复需自行调整类型方言。
- **结果流式背压**：`query.ack` 背压尚未实现，超大结果集依赖 `maxRows` 截断，建议显式限制返回行数。
- **PostgreSQL 表 DDL**：为目录简化重建，与 `pg_dump --schema-only` 存在差异；视图 / 函数 / 触发器的定义编辑需在详情面板查看 DDL 后手动保存 `.sql`，表设计器暂仅覆盖表对象。
- **引擎版本覆盖**：已对照真实 MySQL 8.4.13 与 PostgreSQL 18.7 验收；MariaDB 11、PostgreSQL 13–16 等未逐版本部署（共用驱动路径，未单独验证）。
- **性能验收**：5000 表 / 1000 表元数据等大规模场景、类型矩阵尚未实测。
- **i18n**：中英文覆盖顶栏、侧栏、编辑器工具栏、结果标签、状态栏、设置与危险确认；对象树、历史、表设计器、导入导出对话框的动态文案仍为中文，后续逐步补齐。
- **会话恢复**：恢复激活标签、编辑器滚动与侧栏布局；结果网格与对象树的滚动位置不持久化。
- **DPI**：框架启用 Per-Monitor V2，已做 100% / 150% 目视检查；200% 缩放与多显示器切换尚未留档。
- **崩溃诊断**：仅写文本崩溃日志，暂不生成 minidump；无远程错误上报（本地优先）。
- **远程连接**：暂不支持 SSH 隧道（v1.1 规划），直连需数据库网络可达。

> 开发模式（DevMode）下需将 `frontend/` 置于 exe 同级目录（或 `build\Release\..\..\frontend`）。

## 许可

Silkit 以 MIT 许可证发布，见 [LICENSE](LICENSE)。内置 TauriCPP 框架版权归其原作者所有。
第三方组件（libmariadb LGPL-2.1、libpq、SQLite、CodeMirror、nlohmann/json、WebView2 SDK）的许可与源码地址见 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)。
