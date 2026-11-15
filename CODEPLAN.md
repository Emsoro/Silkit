# Silkit CodePlan：轻量跨库数据库管理工具

> **目标**：基于 **TauriCPP（C++20 + Win32 + WebView2）** 的 StepDraw 工程骨架，开发一个 **单 exe、离线可用、启动快** 的桌面数据库管理工具。首版支持 **MySQL / MariaDB、PostgreSQL、SQLite**，交互对标 **Navicat 的完成度、HeidiSQL 的轻量、DBX/GoNavi 的现代工作台**。
>
> **参考代码**：`D:\Work\CPlusPlus\StepDraw`（框架与前端组织，可直接复用）、`D:\Work\CPlusPlus\Silkit\HeidiSQL-12.22`（只读参考，**不参与构建**）。
>
> **产品名**：**Silkit**（silk + kit，“丝滑的数据库工具箱”）｜ 构建产物 `Silkit.exe` ｜ 数据目录 `%APPDATA%\Silkit` ｜ 代码目录 `D:\Work\CPlusPlus\Silkit`
>
> **规划日期**：2026-11-13 ｜ **当前版本**：v1.0.4（M0–M9 已交付；v1.0.4 关键字候选不再显示右侧标签）｜ **文档状态**：实施基线 + 进度回写
>
> **最近对标**：2026-11-15 对照 `D:\Cplusplus\dbx-0.6.69` 与 `D:\Cplusplus\GoNavi-1.2.2` 完成功能 Gap 梳理（§19），据此重排路线图（§14）并补齐 E2E 测试项与执行入口（§12.4 / §12.9）。

---

## 0. 状态总览

> 图例：⬜ 未开始 ｜ 🟡 进行中 ｜ ✅ 已完成 ｜ ⛔ 阻塞
> 维护规则：完成一项就改状态；实际实现与本文不一致时，在 §0.2 记录偏差，**本文是唯一计划源**。

### 0.1 里程碑

| 里程碑 | 内容 | 状态 | 估算 | 版本 |
|---|---|---|---|---|
| M0 | 工程骨架、构建、异步 IPC、UI 外壳与主题切换 | ✅ | 4–5 d | v0.0.1 |
| M1 | SQLite 垂直切片（连接→树→查询→结果） | ✅ | 5–6 d | v0.1.0 |
| M2 | MySQL / MariaDB 驱动 | ✅ | 6–7 d | v0.2.0 |
| M3 | PostgreSQL 驱动 | ✅ | 6–7 d | v0.3.0 |
| M4 | 查询工作区（编辑器/多结果/历史/补全） | ✅ | 6–7 d | v0.4.0 |
| M5 | 表格数据编辑（CRUD/筛选/分页/单元格查看） | ✅ | 8–10 d | v0.5.0 |
| M6 | 表设计器与 DDL 管理 | ✅ | 8–10 d | v0.6.0 |
| M7 | 导入 / 导出 / 转储 | ✅ | 5–6 d | v0.7.0 |
| M8 | 设置、主题、会话恢复、安全与打磨 | ✅ | 5–6 d | v0.8.0 |
| M9 | 发布 1.0（图标/文档/兼容性/安装包） | ✅ | 3–4 d | v1.0.0 |

> 2026-11-14 回写：M0 全部交付；M1 的「连接 → 对象树 → 查询 → 结果」链路已端到端跑通，当前 `v0.1.0`。**M2 / M3 已实现并对照真实服务器验收**（MySQL 8.4.13 / PostgreSQL 18.0.7）。**M4 已交付并运行时验收**。**M5 已交付并运行时验收**：表格数据编辑（CRUD/筛选/分页/单元格查看/事务回滚）经 CDP 探针对照 MySQL 8.4.13 / PostgreSQL 18.0.7 全绿，CTest `table_ops`（SQLite）通过。期间修复一个影响 M1–M4 的 MySQL 数值列误判为二进制而 base64 编码的潜伏 bug。**M6 已交付垂直切片**：三引擎 DDL 生成器、表设计器（列/索引/外键/选项/SQL 预览）、`ddl.preview`/`ddl.apply` 令牌确认、删除/清空/新建删除库、视图/函数/触发器 DDL 保存 `.sql`；CTest `ddl_builder` 全绿（SQLite roundtrip + MySQL/PG SQL 断言），Mock 后端经 Node 模拟桥联调通过。**M7 已交付垂直切片**：CSV 解析/五种流式导出/导入向导/转储/SQL 文件执行/编码；CTest `io` 69 项检查通过（含 100 万行导出、10 万行导入、错误回滚、整库恢复）。**M8 已交付垂直切片**：设置页（通用/编辑器/查询/网格/导出/关于）、设置 `.bak` 备份与损坏恢复、安全模式危险语句令牌确认、日志脱敏与崩溃日志、侧栏拖拽/折叠与激活标签恢复、中英文主要界面；CTest `m8` 25 项检查与 `contrast_check.py` 125 项对比度全部通过；DPI 200% 与多显示器尚未留档。**M9 已交付**：版本号统一 `1.0.0`（CMake/vcpkg/version.h/前端/Mock）、exe 多分辨率图标与窗口标题、关于页版本与路径、README 功能概览与支持矩阵、`THIRD-PARTY-NOTICES.md`、`tools/check_version.py` 与 `tools/package.ps1` 便携包（`dist/Silkit-1.0.0-win-x64.zip`）、`CHANGELOG.md`；Release 构建下 CTest 9/9、MySQL 8.4.13 与 PostgreSQL 18.0.7 读写/DDL 集成回归全绿、对比度 125 项通过；干净 Windows 11 虚拟机冒烟与 Git tag 待人工执行/提交后完成。

> 2026-11-15 回写：M0–M9 已按 README / CHANGELOG / M9 发布验收统一刷新为「已完成」，此前表里残留的 🟡/🟢 是进度未回写造成的过期状态。**v1.1 起不再沿用 M 编号**，后续工作按 §19 的 Gap 优先级（P0/P1/P2）排期，并同步刷新 §14 路线图。
>
> 2026-11-15 回写（v1.0.2 / v1.0.3 / v1.0.4）：查询上下文与智能补全以 patch 增量交付，不占用 v1.1 里程碑编号。对象树选中库/表后新建查询自动 `USE`；查询页新增连接/库下拉、按标签记忆库并自动切回；补全升级为关键字 + 表/视图 + 列（`表名.` / `别名.`）。同轮修复「启动即弹查询页、关掉又弹一个」的缺陷：改为无标签空态。v1.0.3 再把表引用解析范围从「光标之前」扩展到「光标所在的整条语句」，列候选限定于语句中的表，并在候选右侧显示所属路径。v1.0.4 去掉关键字候选右侧的 `keyword` 标签，`tools/test_hint.js` 扩充到 46 项。新增 `tools/test_tabs.js`（5 项）无浏览器自测；详见 §9.6 / §9.7 / §9.7.1 / §12.2 / §12.4.2 / §14。

**总量**：约 56–68 个开发日（单人，含自测），按每周 5.5 天计约 **10–12 周**。

### 0.2 偏差记录

| 日期 | 计划 | 实际 | 原因 | 处理 |
|---|---|---|---|---|
| 2026-11-13 | §8.5 标注 `app.info`/`settings.get`/`conn.list` 等为同步命令 | 全部统一走 `ipc.call` → worker → `ipc.event` | Bridge handler 在 UI 线程，统一异步更安全，前端 `api.call` 已适配 | 保持实现；同步命令仅 `ipc.call` / `ipc.cancel` 两个入口 |
| 2026-11-13 | §8.4 错误 `code` 为数字（-32601/-32602/-32000） | 命令层用字符串 `code`，DB 错误用原生数字码 | 前端已按字符串处理，且两类错误混用同一字段会冲突 | 命令层错误额外附 `numericCode`，两者并存 |
| 2026-11-13 | §6.7 `query.ack` 背压 | 未实现 | M1 优先打通链路，背压需前端节流与 router 同步命令配合 | 并入 M4 查询工作区 |
| 2026-11-13 | §9.5 未加载节点触发 `meta.search` | 后端命令已实现，前端仍只做已加载节点的本地过滤 | 搜索结果节点渲染属于 M4 编辑器/树联动 | 后端先行，前端并入 M4 |
| 2026-11-13 | `conn.set_database` 语义为切库 | SQLite 仅允许切换到 `PRAGMA database_list` 中已 ATTACH 的库 | SQLite 没有 `USE`；未附加时给出 ATTACH 提示 | 记录语义差异，M2/M3 驱动按各自语义实现 |
| 2026-11-13 | M1-9 后端 `query.history.*` | 前端 `localStorage` + `settings.json` 持久化最近查询 | 历史面板已在 M1 可用，后端历史体系属 M4 | M4 迁移到 `history.jsonl`，保留前端数据导入 |
| 2026-11-13 | §3.2 事务状态 | `conn.stats` 原为硬编码 `autocommit` | 本轮已按 `BEGIN/COMMIT/ROLLBACK` 更新 Session 状态 | 状态栏展示并入 M4 |
| 2026-11-14 | §4 规划 `db/engine_meta.*`、独立 `mysql_driver`/`postgres_driver` | 新增 `db/engine_dialect.*` + `ipc/commands_meta_engine.*`，SQLite 元数据仍留在 `commands_meta.cpp` | 集中方言 SQL/类型/错误映射，避免把 SQLite 分支改坏 | 保持实现；`commands_meta.cpp` 仅做引擎分派 |
| 2026-11-14 | §6.2 `Placeholder()` 用于参数化 | M2/M3 参数化由驱动在 `Execute` 内绑定，`Placeholder()` 仍未被上层调用 | 上层 `RunRows` 用字面量转义的元数据 SQL，不拼接用户输入值 | 保留接口；M4 查询编辑器接入时再使用 |
| 2026-11-14 | M3-5 完整 DDL 组装器 | 表 DDL 由 `pg_catalog` 简化重建（列/类型/默认值/NOT NULL/PK） | `pg_get_tabledef` 不存在，完整对齐 `pg_dump` 需要大量方言细节 | 在 DDL 返回值附 `warnings`，M6 表设计器再补 |
| 2026-11-14 | M2-8 / M3-8 docker 集成测试 | `tests/integration_db.cpp` 按 env DSN 触发，未设置时跳过 | 本机无 MySQL/MariaDB/PG 服务，且 vcpkg 已装静态客户端库 | CI 配置 DSN 后实跑；类型矩阵与性能验收仍待补 |
| 2026-11-14 | `vcpkg.json` `builtin-baseline` = `2016b657…` | 改为本机 vcpkg 仓库存在的 `ef7dbf94…` | 原基线在浅克隆中缺失 `versions/baseline.json`，`build.ps1` 的 manifest 安装失败 | 已验证全新 manifest 构建与 CTest 通过 |
| 2026-11-14 | M2 `MySqlDriver` 二进制判定 | `IsBinaryField` 改用 `field->charsetnr == 63` 判定真二进制 | `information_schema` 的 `longtext` 列（如 `COLUMN_TYPE`）以 `MYSQL_TYPE_BLOB` 返回且被 `BINARY_FLAG` 误判为二进制，导致列类型等文本被 base64 编码 | 对照 MySQL 8.4.13 验证：列类型/默认值/注释等文本正常，真二进制（BLOB/VARBINARY，charset 63）仍 base64 |
| 2026-11-14 | M4-8 `.sql` 读写走通用文件 IPC | 新增 `app.read_file`/`app.save_file`，仅允许 `.sql/.txt/.csv/.tsv/.json/.md` 且 ≤16MB | WebView2 前端无任意文件读取能力，需要后端配合；为避免被当任意文件通道，收紧扩展名与大小 | 拖放打开仍走前端 `FileReader`，对话框路径走 IPC |
| 2026-11-14 | M4-11 完整导出向导 | 先做前端 Blob 下载 CSV/JSON 简易导出 | M7 才接入完整导出（流式、进度、格式选择）；当前仅覆盖当前结果集 | 分隔符取 `settings.export.csvDelimiter`，完整导出并入 M7 |
| 2026-11-14 | M4-3 语句分割复用后端 `SplitSql` | 前端自实现 `SilkitSqlTools.splitStatements`（引号/注释感知，语义对齐后端） | 后端 `query.execute` 仍做权威分割；前端仅用于「当前语句」定位与展示，避免一次 IPC 往返 | 双实现需保持语义一致，M5+ 如有不一致以后端为准 |
| 2026-11-14 | MySQL 数值列被 base64 编码（M1–M4 潜伏 bug） | `mysql_driver` 读取结果列时 `real_binary = (charsetnr == 63)`，但 MySQL 数值/日期/时间/枚举/集合/JSON 列的 `charsetnr` 本就是 63，被误判为二进制而 base64 编码（表现为数字显示为乱码、`table.count` 恒为 0） | 结果读取与 `IsBinaryField` 仅在「真正字符串/BLOB 类型（`MYSQL_TYPE_STRING/VAR_STRING/BLOB/GEOMETRY` 等）且 `charsetnr==63`」时判为二进制 | 修复后 MySQL 数值结果正确显示为文本，COUNT/估算正常；同时修正 `table.count` 返回 0 与 `totalEstimate` 为 null 的问题 |
| 2026-11-14 | M5 后端/前端已实现但未提交 | `commands_table.cpp`/`table_ops.cpp`/`grid.js`/`table.js`/测试等 M5 文件在上一轮开发后处于未提交状态 | 按用户要求开发 M5 并验收、回写文档、推送 | 已补构建、CTest（5/5）、MySQL/PG 双引擎 CRUD 运行时验收 |
| 2026-11-14 | M6 设计器只覆盖「表」对象 | `ddl.load`/`ddl.preview`/`ddl.apply` 仅接受 `kind=table`；视图/函数/触发器仍走详情面板查看 DDL 并可保存 `.sql` | 修改视图/函数/触发器定义需要各自的编辑器语义，超出表设计器范围 | 后续如需编辑视图定义，另起专用编辑器复用 `ddl.preview` 令牌机制 |
| 2026-11-14 | M6 三引擎 roundtrip | SQLite/MySQL/PG 均实跑建表→改表→删表/清空；MySQL/PG 通过 `integration_db` 连接真实服务器 | 需要真实服务复测 ALTER 的方言差异 | MySQL 8.4.13 / PostgreSQL 18.0.7 全部通过（0 failures） |
| 2026-11-14 | MySQL 外键元数据查询失效 | `information_schema.KEY_COLUMN_USAGE` 在 MySQL 8 没有 `UPDATE_RULE/DELETE_RULE`，查询报 1054 被静默忽略，导致外键列表为空 | 需要关联 `REFERENTIAL_CONSTRAINTS` 获取级联规则 | 已修复 `ListForeignKeysSql`；MySQL 外键现可正确加载与展示 |
| 2026-11-14 | MySQL 外键隐式索引 | 外键无可用索引时 MySQL 自动创建同名索引，被当成用户索引，ALTER 时 `DROP INDEX` 报 1072 | 设计器不应管理外键自动索引 | 加载时过滤「名称与列均与外键一致」的索引；ALTER 顺序改为先删外键再删索引 |
| 2026-11-14 | M6 ALTER 的 SQLite 能力边界 | 修改列类型/NOT NULL/默认值、调整主键、增删外键在 SQLite 下直接返回明确错误 | SQLite 不支持这些 ALTER 操作，静默降级会误导用户 | 提示「请重建表」；MySQL/PG 走 `MODIFY/CHANGE COLUMN` 与 `ALTER COLUMN` |
| 2026-11-14 | §10.4 预览影响行数 | `DdlPlan` 增加 `affected_rows`，IPC 返回 `affectedRows`；CREATE 为 `0`，DROP/TRUNCATE 执行前 `SELECT COUNT(*)`，ALTER/建库删库为“未知” | 纯生成器无法预估行数；COUNT 对超大表有开销，但预览准确性优先 | 设计器与确认弹窗均展示“影响行数”；Mock 后端同步返回 |
| 2026-11-14 | §10.6 新对象名严格校验 | `SafeName` 增加 UTF-8 校验、引擎长度上限（MySQL 64 / PG 63 / SQLite 255 字节）、拒绝引号与控制字符 | 既有对象名若包含引号，编辑该表时会被拒绝并提示原因 | 错误信息明确；如需支持此类历史对象，应改为仅校验收窄的新名称字段 |
| 2026-11-14 | M8 中/英 i18n 覆盖主要界面 | 顶栏、侧栏、编辑器工具栏、结果标签、状态栏、设置对话框与危险确认已接入 `data-i18n`；对象树、历史、表设计器、导入导出对话框的动态文案仍为中文 | 逐视图抽取文案工作量较大，先保证外壳与设置可用 | 后续按视图逐步补齐；字典集中在 `frontend/js/i18n.js` |
| 2026-11-14 | M8 会话恢复含滚动位置 | 持久化激活标签、编辑器滚动与侧栏宽度/折叠；结果网格与对象树滚动位置未持久化 | 网格/树使用虚拟滚动，恢复时机与加载完成顺序耦合 | 视为可接受偏差；如需恢复网格位置，应在 `setSet` 后按行索引回滚 |
| 2026-11-14 | M8 崩溃日志 | 未处理异常写文本 `logs/crash-*.log`（版本/时间/异常码/地址/线程），未生成 minidump | 生成 dump 需引入 DbgHelp 与符号/转储策略，文本日志已满足定位入口 | 后续可在发布版追加可选 minidump |
| 2026-11-14 | M8 安全模式拦截范围 | `query.execute` 与 `query.execute_file` 执行前统一检查；导入 CSV、DDL 应用、转储等显式操作仍走各自的确认流程 | 这些操作本身已在 UI 中要求确认或为显式动作 | 如需统一，可在 `import.csv`/`ddl.apply` 复用 `CheckSafeMode` 令牌 |
| 2026-11-14 | M9-2 README 含截图 | README 提供功能概览、支持矩阵与发布验收清单，未附截图 | 仓库内没有可直接引用的发布截图，避免引用失效图片 | 截图留待发布页补充；文字描述已覆盖全部能力 |
| 2026-11-14 | M9-4 干净 Windows 11 虚拟机冒烟 | 未执行（当前无可用虚拟机），仅在本机 Release 产物上验证启动、标题、图标与版本资源 | 需要独立干净环境与可选无 WebView2 场景 | 验收清单写入 README「发布验收清单」，待人工在 VM 执行并回写 |
| 2026-11-14 | M9-6 安装包 | v1.0 只产出 portable zip（`dist/Silkit-1.0.0-win-x64.zip`），不提供安装程序 | 与计划一致（安装包排期 v1.1） | 解压即用；WebView2 缺失由应用内中文引导 |
| 2026-11-14 | M9-8 Git tag | `CHANGELOG.md` 已建立，`v1.0.0` 标签需在本次变更提交后创建 | 未提交的工作区无法打标签 | 提交后执行 `git tag -a v1.0.0 -m "Silkit 1.0.0"` |
| 2026-11-14 | `build.ps1` 固定 `VCPKG_MANIFEST_MODE=ON` | 改为读取 `build/CMakeCache.txt` 的 `Z_VCPKG_CHECK_MANIFEST_MODE` 沿用首次配置模式；全新目录若已按 classic 装好依赖则用 `OFF` | vcpkg 禁止在已配置目录切换 manifest 模式，原脚本在 classic 预装依赖的机器上必然报错 | 两种模式均可构建；需强制 manifest 时先 `-Clean` 删除 `build/` |
| 2026-11-15 | §12.1 测试环境为 Docker `mysql:8.4` / `postgres:16`，集成测试读 `SILKIT_TEST_MYSQL_DSN` / `SILKIT_TEST_PG_DSN` | 本机直连 `C:\mysql-8.4.13-winx64`（8.4.13，`employees` 30 万员工 / 284 万薪资）与系统 PostgreSQL 18.7（`postgres/123456`）；实际实现读 `SILKIT_MYSQL_*` / `SILKIT_PG_*` 分项变量 | 本机已有可用服务，Docker 方案与代码里的环境变量名都不一致 | 以本机真实服务为 E2E 基准（§12.1），Docker 仅保留作 CI 备选；新增 `tools/e2e_runner.py` 统一注入变量 |
| 2026-11-15 | 系统 PostgreSQL 应有 `employees` 库 | `\l` 实测只有 `postgres` / `silkit_test` / `template0` / `template1` | 用户口述的测试源与实际环境不符 | 新增 `tests/e2e/seed_pg_employees.sql` 精简镜像（9 部门 / 12 员工 / 18 薪资行），`e2e_runner.py --seed-pg-employees` 建库；runner 自动探测，缺失时退回 `silkit_test` 并告警 |
| 2026-11-15 | §12.4 计划用 Playwright + `mock.js` 跑 UI/E2E | 先在真实库落地 `tests/e2e/e2e_real_db.cpp`（14 项 × 双引擎 = 31 项），Playwright/CDP 用例保留为前端自动化待办与手工清单 | 仓库没有 Playwright 依赖；而当前最缺的是「真实服务器上的后端行为」回归 | 后端 E2E 已纳入 CTest（`e2e_real_db`）；前端 Playwright 归入 §19 P1，验收矩阵见 §12.4 / §12.5 |
| 2026-11-15 | §6.7「首批携带列元数据」三引擎一致 | E2E-05 发现 PostgreSQL 对 **0 行 SELECT** 不回传列头（单行模式只在 `PGRES_SINGLE_TUPLE` 取元数据，空结果只有 `PGRES_TUPLES_OK`） | `postgres_driver.cpp` 未处理 `PGRES_TUPLES_OK`；MySQL 侧正常 | 已修：`capture_columns()` 同时用于两种状态，空结果补发带列头的批次；E2E-05 双引擎转绿 |
| 2026-11-15 | 状态栏展示 PostgreSQL 服务端版本 | `ServerVersion()` 把 `PQserverVersion=180007` 格式化成 `18.0.7` | 三段式格式化只适用于 PG 9.x；PG 10+ 的版本号是 `major*10000+minor` | 已修：`>=100000` 时输出 `major.minor`，实测显示 `18.7` |
| 2026-11-15 | §12.2 单元测试清单 | 表里列的 `test_sql_splitter` / `test_identifier` / `test_type_map` / `test_csv` 从未落地，实际是 10 个 CTest 目标 | 规划清单未随实现回写 | §12.2 已按 `tests/CMakeLists.txt` 重写；纯逻辑检查集中在 `silkit_smoke` / `engine_dialect` / `io` / `m8` |
| 2026-11-15 | MySQL 连接 `ssl_mode` 语义（`prefer` 可降级 / `disable` 可关 TLS） | 连接 3307 的 SQLTiny（MySQL 协议、握手不声明 `CLIENT_SSL`）时，连 `disable` 都报 `2026 TLS/SSL error: SSL is required, but the server does not support it` | vcpkg 的 libmariadb 是 **Connector/C 3.4.5**：默认要求校验证书（`tls_allow_invalid_server_cert=0`），认证阶段无条件把 `use_ssl` 置 1，使 `MYSQL_OPT_SSL_ENFORCE=0` 单独设置失效（实测三种设置结果相同） | 已修 `mysql_driver.cpp`：在 `mysql_ssl_set()` **之后**显式写 `MYSQL_OPT_SSL_VERIFY_SERVER_CERT` + `MYSQL_OPT_SSL_ENFORCE`（prefer=1/0 自动降级，disable=0/0 明文，require/verify-*=1/1 严格）；`DecorateError` 对 2026 补可行动提示；新增可选 E2E `SSL-01~03` |
| 2026-11-15 | 连接失败错误信息只展示 `message + detail` | 错误条新增最右侧「复制信息」按钮，错误 toast 也可复制；复制内容为完整诊断（时间/引擎/主机/端口/用户/库/SSL 模式/字符集 + 错误码/SQLSTATE/详情/提示，**不含密码**），并补显示 `hint` | 报障时用户只能截图，缺失原生错误码与上下文；`api.js` 已经透传 `code/sqlState/hint` 但连接视图未用 | 手工验收项 U-13；`U.copyText` 改为始终返回 Promise，保证 WebView2 下 `execCommand` 回退的失败可被 `.catch()` 捕获 |
| 2026-11-15 | `CMakeLists.txt` 与文档的编译标准 | `CMAKE_CXX_STANDARD 17` → **20**（`REQUIRED ON` / `EXTENSIONS OFF` 不变）；README、§0 / §12.1 / §16.1 / §19.1 及 CHANGELOG 的对应文字同步 | 项目统一升级到 C++20；TauriCPP 内置副本源码由顶层 `file(GLOB)` 直接编译，自动继承顶层标准，其自带 `CMakeLists.txt`/README 属于上游框架说明、不参与 Silkit 构建，保留 C++17 表述 | 全量 Release 重建 + `ctest -C Release` 10/10 通过、`e2e_real_db` 31 PASS；MSVC 需 VS 2022（`/std:c++20`） |
| 2026-11-15 | §14 v1.1/v1.2/v2.0 排期 | 按 dbx / GoNavi 对标结果重排优先级 | 原排期凭直觉，未区分「日常必需」与「平台化能力」 | 见 §19 Gap 矩阵与 §14 新路线图；明确列出「不做」项及理由 |
| 2026-11-15 | 版本号 `1.0.0` 与 CHANGELOG「未发布」段 | 递增为 **`1.0.1`** 并正式发版；README 新增版本说明表并同步发布包/验收清单/已知限制标题，§0.3 / §16.1 / §16.2 / §19.1 同步 | 未发布段已有 SSL 修复、连接诊断复制与 C++20 升级三项交付，patch 递增即可；`v1.1` 已排给 SSH / ER 图等里程碑，不宜占用 | `tools/check_version.py` 校验 8 处一致；`tools/package.ps1` 从 CMake 读取版本号，产出 `dist/Silkit-1.0.1-win-x64.zip` |
| 2026-11-15 | §9.7 补全候选为「关键字 + 已加载表/视图」，列补全排到 v1.1 | v1.0.2 重写 `frontend/js/editor/hint.js`：候选含关键字 + 当前库表/视图 + 列；`表名.` / `别名.` 只提示该表列（带类型说明、插在点号后）；`FROM/JOIN/UPDATE/INTO` 中的表列进入裸标识符候选；PG schema 透传 | 原实现只按 `meta.children` 缓存少量表，写跨库 SQL 时命中率低 | 元数据按 `会话｜引擎｜库｜schema` 异步按需缓存，切库 / DDL 变更自动失效；`tools/test_hint.js` 31 项自测（§12.2）；触发延迟 500ms → 220ms，无连接时不拉起 anyword 兜底 |
| 2026-11-15 | 新建查询默认使用当前活动会话与当前库 | v1.0.2 新建查询继承对象树选中的连接 / 库 / 表，不一致时自动切库（`conn.set_database`：MySQL 内部 `USE`、PG 重连、SQLite 限已 ATTACH 库）；查询页新增「连接 / 库」下拉，切换即时生效；标签激活 / 执行 SQL 前自动校正会话库 | 多标签共用一个会话，A 标签切库会污染 B 标签；从对象树打开查询还要手工切库 | 新增 `frontend/js/views/query_context.js`（下拉、切库、补全缓存刷新、库列表），`tabs.js` 在标签上持久化 `database / schema / connectionId / table`，`tree.js` 暴露选中上下文；见 §9.7.1 |
| 2026-11-15 | 无标签时自动补一个查询页 | v1.0.2 修复：启动、连接成功、关闭最后一个标签都不再自动建查询页；改为显示 `#workspaceEmpty` 空态卡片（新建查询 / 打开 SQL / `Ctrl+N` 引导），空标签集随会话恢复持久化，重启后仍保持关闭 | `tabs.init()` 无标签时建默认查询页、`closeTab()` 关闭最后一个标签后又 `openQuery()`、`onOpen()` 连接成功也建一个，造成「一打开就有查询页、一点关就又弹出来一个」 | 去掉三处自动建页，新增空态视图与 5 项无浏览器回归自测 `tools/test_tabs.js`；关闭非激活标签时改用 `state.activeTab()` + `activate()`，顺带修正原来误切到相邻标签的问题 |
| 2026-11-15 | 输入 `employees.` 弹出非该表的列 | v1.0.2 修复：点号补全严格化——`表名.` / `别名.` 只返回已确认存在的表的列，解析不到即返回空；`FROM/JOIN/UPDATE/INTO` 后的 `库名.`（PG `schema.`）识别为库限定符并提示该库的表名；裸标识符只补当前库中真实存在的表的列 | `resolveTable` 解析失败时把限定符本身当表名查列；`tableRefs` 用 `[`"\w.$]+` 吞掉结尾点号，把 `FROM employees.birth_date` 的列名误判成表；裸标识符分支又把这些伪表的列塞进候选，造成「串列」 | 重写 `frontend/js/editor/hint.js` 的限定符解析与严格解析；补 `loadScopes` 缓存库/schema 名；`tools/test_hint.js` 扩充到 31 项（§12.2） |
| 2026-11-15 | §14 路线图只列到 v1.0 | 补 v1.0.1 / v1.0.2 两个 patch 版本行 | 连接诊断、SSL、主题确认框、C++20、查询上下文与补全都以 patch 形式交付，未建里程碑 | §14 表格新增两行，v1.1 仍留给 P0 完成度项 |
| 2026-11-15 | 补全只解析光标之前的文本；候选名称带 `(table)` / `(view)` | v1.0.3 修复：`statementAt()` 以分号切分光标所在的整条语句（含光标之后的 `FROM`）并剥离注释，列候选只取语句中真实出现的表；`show-hint` 自定义 `render` 把类型与所属路径右对齐 | SELECT 列表写在 FROM 之前，输入 `departments.dept_no, d` 时 FROM 还没进入解析范围，导致 `dept_name` 不出现；候选右侧缺少来源信息 | `tools/test_hint.js` 扩充到 45 项（含用户实测 SQL、多语句与注释隔离、跨库/PG 路径、render DOM）；见 §9.7 / §12.2 |
| 2026-11-15 | 关键字候选右侧显示冗余的 `keyword` 标签 | v1.0.4 修复：关键字不再携带 `_source` / `_badge`，`renderHintItem()` 只生成左侧名称 | 关键字本身已可识别，右侧标签挤压来源路径的视觉层级 | `tools/test_hint.js` 增加到 46 项（含关键字 render DOM 断言）；见 §9.7 / §12.2 |

### 0.3 版本标识位置

| 位置 | 内容 |
|---|---|
| `CMakeLists.txt` | `project(Silkit VERSION 1.0.4 LANGUAGES CXX)` |
| `src/app/version.h` | `SILKIT_VERSION` 兜底宏 |
| `src/main.cpp` | 启动日志、窗口标题、`app.info` |
| `frontend/index.html` | 右下角状态栏版本号（小胶囊徽章样式），由 `app.info` 填充 |
| `README.md` / `README.en.md` | 版本说明表、支持矩阵、发布包名、已知限制标题 |
| `CHANGELOG.md` | 各 patch 版本交付说明 |
| `tools/package.ps1` | 发布包名从 CMake 读取，无需手工同步 |
| `vcpkg.json` | `"version-string"` 与 CMake 同步 |

---

## 1. 产品定位与范围

### 1.1 一句话

**一个 10MB 以内、双击即用、界面好看的 Windows 数据库工作台**：连上 MySQL/PostgreSQL/SQLite，浏览对象、写 SQL、改数据、改表结构、导入导出，不需要 Java、Python、Node 或浏览器运行时。默认外观为 **晨樱浅色**（低饱和玫瑰 + 梅子墨色），把“专业工具”做出时尚感。

### 1.2 目标用户与场景

| 用户 | 核心场景 | 关键诉求 |
|---|---|---|
| 后端/全栈开发 | 本地或测试库查数据、改数据、跑迁移 SQL | 启动快、SQL 编辑舒服、结果网格好用 |
| 运维/DBA | 临时排障、看表结构、导出数据 | 多库连接、取消慢查询、DDL 预览 |
| 数据分析/测试 | 查数、导出 CSV/JSON、构造测试数据 | 大数据量不卡、复制粘贴方便 |
| 内网用户 | 无法安装 Java/Electron 的受控环境 | 单文件、免安装、离线 |

### 1.3 参考产品取舍

| 参考 | 值得借鉴 | 不照搬 |
|---|---|---|
| **Navicat** | 连接树信息密度、表设计器、导入导出向导、对象右键菜单完整度 | 功能过重、收费、启动慢 |
| **HeidiSQL** | 轻量 Win32、表格数据直接编辑、批量 SQL 执行、SQL 日志、表设计器 | 界面陈旧、仅 Windows、无现代编辑器 |
| **DBX 0.6.69** | 单二进制约 25MB、CodeMirror 6、虚拟表格、明暗主题、连接加密、导入导出；此外有 ER 图、Schema Diff、数据同步、定时备份、AI、MCP、签名插件市场、100+ 数据源 | Rust/Tauri/CMake 体系不同，不能直接复用；平台化能力与「单 exe 离线轻量」定位冲突 |
| **GoNavi 1.2.2** | Wails（系统 WebView）+ Web 前端架构、虚拟大数据网格、可选驱动代理（45+ 数据源）、AI/MCP/插件、数据同步与 SQL 审计、Web/CLI 部署 | Go/React 技术栈、内存占用偏高；后台调度与服务端形态不照搬 |
| **StepDraw** | TauriCPP 壳、前端资源内嵌、Bridge、DPI、开发模式、单文件分发 | 画布/图形相关代码与数据库无关，不复用 |

**Silkit 的定位**：HeidiSQL 的轻量 + Navicat 的完成度 + DBX/GoNavi 的现代 Web UI。

> **对标口径（2026-11-15）**：dbx / GoNavi 已是「数据库工作平台」而非轻量客户端（详见 §19），不能逐项照抄。对标只取「单机桌面 + 已支持的三引擎」范围内用户可感知的能力；异构数据源、云同步、Web/CLI/MCP、插件市场、后台调度等按 §19 判定为 P2 或「明确不做」。

### 1.4 v1 范围

**做：**
- 连接管理：MySQL/MariaDB、PostgreSQL、SQLite；测试连接、保存密码、颜色/分组、只读模式。
- 对象浏览：库/模式/表/视图/函数/存储过程/触发器；懒加载、搜索、右键菜单、DDL 查看。
- 查询工作区：多标签、语法高亮、**新建查询自动继承对象树选中的连接与库（`USE`）**、编辑器工具条「连接 / 库」下拉与按标签记忆库、关键字 + 表/视图 + 列智能补全（`表名.` / `别名.`）、执行当前/选中/全部、多结果集、消息、取消、历史、片段、`.sql` 文件打开/保存、拖放；无标签时显示空态卡片，不自动建查询页。
- 结果网格：虚拟滚动、列宽调整、排序/筛选、复制（TSV/CSV/JSON/INSERT）、单元格查看（长文本/JSON/BLOB 预览）、结果导出。
- 表数据：分页、服务端筛选/排序、插入/更新/删除、事务内提交/回滚、SQL 预览、无主键只读提示。
- 表设计器：列/索引/外键/表选项、SQL 预览、创建/修改/删除/重命名/清空。
- 导入导出：CSV/JSON/SQL/Markdown/HTML；CSV 导入向导；整库/整表 `.sql` 转储。
- 设置：主题（5 套预设，默认晨樱浅色 / 夜樱深色）、语言（中/英）、编辑器/查询/网格默认值、会话恢复、日志。
- 安全：DPAPI 保存密码、破坏性操作确认、危险 SQL 警告、日志脱敏、只读连接。

**暂不做（放入 v1.1+）：**
- SSH 隧道、代理、SSL 客户端证书 UI（先支持 sslmode 基础项）。
- ER 图、Schema Diff、数据对比、数据传输/同步。
- 用户/权限管理、服务器监控、慢日志分析。
- XLSX 导入导出、Excel 粘贴。
- AI 助手、MCP Server、插件/自定义驱动。
- macOS/Linux（TauriCPP 是 Win32 + WebView2，跨平台需另做壳）。
- SQL Server / Oracle / ClickHouse / Redis 等（通过 Driver 接口预留，不在 v1）。

### 1.5 设计原则

1. **本地优先**：只做数据库客户端，不引入账号、云同步、遥测；配置全部落本地。
2. **轻量**：单 exe 内嵌前端，无 Java/Node/Python；冷启动目标 < 1.5s，安装体积 < 10MB。
3. **显式安全**：生成 SQL 必须可预览；破坏性操作二次确认；密码不出 C++ 进程。
4. **渐进加载**：对象树懒加载；查询结果流式分批；网格虚拟滚动；大字段按需拉取。
5. **驱动隔离**：所有数据库差异收敛在 `src/db/drivers/*`，UI 只认统一模型。
6. **可测试**：SQLite 内存库做单元测试；MySQL/PostgreSQL 用 Docker 做集成测试。
7. **可扩展**：DriverRegistry、Capabilities、命令注册表，为后续数据库/MCP 留接口。

---

## 2. 技术选型与决策记录（ADR）

| 编号 | 决策 | 理由 | 代价/备注 |
|---|---|---|---|
| ADR-01 | 桌面壳复用 **TauriCPP**（Win32 + WebView2） | 已有单文件、资源内嵌、Bridge、DPI、开发模式；与 StepDraw 一致 | 仅 Windows；依赖 WebView2 Runtime |
| ADR-02 | 数据库访问使用**原生 C 客户端库** | 成熟、协议完整、性能好；不重复造轮子 | 静态链接与 CRT 需在 M0 验证 |
| ADR-03 | MySQL 用 **libmariadb**，不用 libmysql | LGPL-2.1，协议兼容 MySQL 5.7/8.x/MariaDB | 静态链接需满足 LGPL 重链接义务；商业分发需评估 |
| ADR-04 | 所有 DB 操作走**异步 IPC**（reqId + 事件） | Bridge handler 在 UI 线程同步执行，直接查库会卡死窗口 | 需新增 AsyncRouter；前端 `api.call` 封装 |
| ADR-05 | 结果值全部以**字符串 + 类型元数据**传输 | JS Number 无法安全表示 BIGINT/DECIMAL(38) | 前端负责显示格式化；编辑时回传字符串 |
| ADR-06 | SQL 编辑器用 **CodeMirror 5**（vendor 预置） | 单文件、无构建步骤、SQL 模式/补全/搜索齐全 | 体积约 300KB；v1.1 可评估 CM6 |
| ADR-07 | 结果网格**自研 DOM 虚拟滚动** | 不引入 React/Vue/AG Grid；与 StepDraw 原生风格一致 | 需要自己处理列宽、选择、编辑、复制 |
| ADR-08 | 密码用 **Windows DPAPI** 加密存储 | 用户绑定、无需主密码、系统原生 | 换机器不可解密；导出连接默认不含密码 |
| ADR-09 | **一个连接一个工作线程 + 任务队列** | 驱动对象非线程安全；天然串行化事务/会话状态 | 连接多时线程多；v1 限制同时打开连接数 |
| ADR-10 | 元数据**懒加载 + TTL 缓存 + DDL 后失效** | 大库（数千表）不能一次性拉全 | 需要刷新按钮与缓存失效策略 |
| ADR-11 | 配置用 **JSON/JSONL + 原子写**，不用注册表 | 可读、可备份、易迁移；与 StepDraw 一致 | 需要 schema version 与迁移函数 |
| ADR-12 | 前端用**原生 JS（IIFE 命名空间）+ 模块化文件** | 零构建、改完刷新即可；符合 StepDraw 习惯 | 需要自律的模块边界；不用 TypeScript |
| ADR-13 | 发布形态：**单 exe + 前端资源内嵌**，依赖系统 WebView2 | 与 StepDraw 完全一致；源码不泄露 | WebView2 缺失需引导安装 |
| ADR-14 | 许可证待定：应用代码 MIT/Apache-2.0；第三方遵守各自许可 | 便于开源/内部分发 | libmariadb LGPL、libpq PostgreSQL、SQLite Public Domain |
| ADR-15 | 主题 = **CSS 变量 + `data-theme`/`data-appearance`**，默认「晨樱」浅色，配色偏时尚风 | 换肤零成本、无构建；默认外观形成产品识别 | 需保证对比度与数据可读性，语义色不得复用主色 |

**不采用的方案：**

| 方案 | 不采用原因 |
|---|---|
| Go/Rust sidecar 进程 | 增加进程管理、打包体积、IPC 复杂度；C++ 已能直连 |
| 自行实现 MySQL/PostgreSQL 协议 | 工作量巨大，协议边界（认证/SSL/类型）风险高；SQLTiny 的经验只作参考 |
| Electron / Tauri(Rust) | 体积大、需要额外运行时；与 StepDraw 框架不一致 |
| Monaco Editor | 体积大、需要 worker 配置；CM5 足够 |
| 嵌入 Chromium | 体积 100MB+；WebView2 已随系统 |
| 数据库连接池 | 桌面客户端单会话即可；池会增加事务隔离复杂度 |

---

## 3. 总体架构

### 3.1 分层

```
┌───────────────────────────── Silkit.exe（单文件） ─────────────────────────────┐
│ WebView2（frontend/ 资源内嵌）                                                │
│   index.html + css + js + vendor/CodeMirror                                  │
│      │ window.__tauricpp__.invoke / listen                                   │
│      ▼                                                                        │
│ TauriCPP Bridge（复用，不改核心）                                             │
│      │ ipc.call / ipc.cancel                                                  │
│      ▼                                                                        │
│ AsyncRouter（工作线程池、reqId、取消、进度事件）                               │
│      │                                                                        │
│      ├── SessionManager ── Session[mysql]    ── MySqlConnection    ── libmariadb
│      │                  ├── Session[postgres] ── PgConnection      ── libpq
│      │                  └── Session[sqlite]   ── SqliteConnection  ── sqlite3
│      ├── MetadataProvider（按引擎）                                            │
│      ├── TableOps / DdlBuilder（按引擎）                                       │
│      ├── Exporter / Importer / Dump（流式）                                    │
│      └── Store（profiles/settings/history/snippets + DPAPI）                   │
└───────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 线程模型

| 线程 | 职责 | 规则 |
|---|---|---|
| UI 线程 | Win32 消息、WebView2、Bridge handler | **绝不做数据库 IO、文件大 IO** |
| AsyncRouter 线程池（4） | 连接测试、元数据、导出/导入等短任务 | 任务可取消；结果通过事件回传 |
| Session 线程（每连接 1 个） | 该连接上的所有驱动调用、事务、查询 | 驱动对象只允许在此线程访问 |
| 取消通道 | UI 线程 → 驱动的 `Cancel()` | 驱动内部保证线程安全（PGcancel / KILL QUERY / sqlite3_interrupt） |

```
UI thread ── invoke("ipc.call") ──> AsyncRouter ──enqueue──> Session thread ──> driver
    ▲                                                              │
    └──────────── Bridge::Emit("ipc.event") ◄──────────────────────┘
```

- `Bridge::Emit` 内部通过 `ExecuteJs` 投递到 UI 线程（框架已实现），可从任意线程调用。
- 应用退出：设置 `shutting_down` → 取消所有任务 → join 线程 → 关闭连接 → 原子写配置。
- 禁止在 Session 线程同步等待 UI 线程，避免死锁。

### 3.3 职责边界

| 层 | 负责 | 不负责 |
|---|---|---|
| 前端 | 布局、状态、编辑器、网格渲染、交互、i18n | 拼接业务 SQL、持有明文密码、直接文件 IO |
| IPC 层 | 参数校验、reqId 路由、错误包装、进度节流 | 数据库协议细节 |
| Session/驱动 | 连接、SQL 执行、元数据、取消、类型转换 | UI 展示、分页策略 |
| 存储层 | 配置/历史/片段读写、DPAPI、原子写 | 连接状态 |
| 导出/导入 | 流式读写、编码、进度、取消 | 业务规则判断 |

### 3.4 启动流程

1. `WinMain` → `tauricpp::App`（1120×700、窗口底色 `#FFF7F9` 晨樱浅色、DevTools）。
2. 设置单实例互斥体（`Local\Silkit.SingleInstance`），第二次启动激活已有窗口并退出。
3. 初始化路径、日志、设置、连接配置；检测 WebView2 Runtime（失败给出中文提示与下载链接）。
4. 注册 IPC 命令；启动 AsyncRouter。
5. 前端 `app.info` → 渲染欢迎页/最近连接 → 恢复 `ui-state.json` 中的标签页（连接按需重连）。
6. `app.ready` 事件后允许交互。

---

## 4. 目录结构

```
D:\Work\CPlusPlus\Silkit\
├─ CODEPLAN.md                  # 本文（唯一计划源）
├─ README.md                    # 使用说明/构建/支持矩阵
├─ CMakeLists.txt               # 主构建（TauriCPP + 3 驱动 + 前端打包）
├─ build.ps1                    # 一键构建（MSVC + vcpkg + CMake）
├─ vcpkg.json                   # manifest：libmariadb/libpq/sqlite3/webview2
├─ .clang-format
├─ .gitignore
├─ TauriCPP\                    # 从 StepDraw 复制并锁定；仅极少量补丁
│  ├─ include\tauricpp\...
│  ├─ src\...
│  ├─ third_party\nlohmann\json.hpp
│  └─ tools\pack_resources.py
├─ HeidiSQL-12.22\              # 只读参考源码（不加入构建、不改动）
├─ src\
│  ├─ main.cpp                  # WinMain + 命令注册入口
│  ├─ app\
│  │  ├─ app_context.h/.cpp     # 组合根：Store/Router/SessionManager
│  │  ├─ version.h
│  │  └─ single_instance.h/.cpp
│  ├─ ipc\
│  │  ├─ async_router.h/.cpp    # reqId/线程池/取消/事件/背压
│  │  ├─ commands_app.cpp       # app.*、settings.*、ui_state.*
│  │  ├─ commands_conn.cpp      # conn.*
│  │  ├─ commands_meta.cpp      # meta.*
│  │  ├─ commands_query.cpp     # query.*
│  │  ├─ commands_table.cpp     # table.*
│  │  ├─ commands_ddl.cpp       # ddl.*
│  │  └─ commands_io.cpp        # io.*（对话框/导入导出/转储）
│  ├─ db\
│  │  ├─ types.h                # ValueKind/Cell/ColumnMeta/ExecResult/DbError
│  │  ├─ params.h               # ConnectParams/Param
│  │  ├─ driver.h               # Driver/Connection/Capabilities 接口
│  │  ├─ driver_registry.h/.cpp
│  │  ├─ session.h/.cpp         # 线程亲和 + 任务队列 + 事务状态 + 元数据缓存
│  │  ├─ session_manager.h/.cpp
│  │  ├─ sql_splitter.h/.cpp    # 多语句拆分（引号/注释/$tag$/DELIMITER）
│  │  ├─ identifier.h/.cpp      # 各引擎标识符引用与转义
│  │  ├─ type_map.h/.cpp        # 原生类型 → ValueKind / 显示规则
│  │  ├─ ddl_builder.h/.cpp     # CREATE/ALTER 生成
│  │  ├─ table_ops.h/.cpp       # 行 CRUD/筛选/排序 SQL 生成
│  │  ├─ meta_provider.h        # 元数据接口
│  │  └─ drivers\
│  │     ├─ mysql_driver.h/.cpp
│  │     ├─ postgres_driver.h/.cpp
│  │     └─ sqlite_driver.h/.cpp
│  ├─ ops\
│  │  ├─ exporter.h/.cpp        # CSV/JSON/SQL/Markdown/HTML（流式）
│  │  ├─ importer.h/.cpp        # CSV 导入（预览/映射/批处理/事务）
│  │  ├─ dump.h/.cpp            # 结构+数据 .sql 转储
│  │  ├─ csv.h/.cpp             # RFC4180 解析/写出/分隔符探测
│  │  └─ job.h/.cpp             # 后台作业、进度、取消
│  ├─ store\
│  │  ├─ paths.h/.cpp           # %APPDATA%\Silkit
│  │  ├─ profiles.h/.cpp        # connections.json + 迁移
│  │  ├─ settings.h/.cpp        # settings.json
│  │  ├─ ui_state.h/.cpp        # ui-state.json（标签页/布局）
│  │  ├─ secrets.h/.cpp         # DPAPI 加解密
│  │  ├─ history.h/.cpp         # history.jsonl（查询历史）
│  │  └─ snippets.h/.cpp        # snippets.json
│  └─ util\
│     ├─ utf8.h/.cpp            # UTF-8 ↔ UTF-16/ANSI（Win32 边界）
│     ├─ strings.h/.cpp         # trim/split/join/大小写/路径
│     ├─ base64.h/.cpp
│     ├─ logger.h/.cpp          # UTF-8 日志、轮转、脱敏
│     ├─ json_util.h            # nlohmann 辅助（安全取值）
│     └─ time.h/.cpp
├─ frontend\
│  ├─ index.html
│  ├─ css\
│  │  ├─ tokens.css             # 语义 token（颜色/字号/间距/圆角/阴影）
│  │  ├─ themes\               # bloom / noir / lilac / mocha / mint 调色板
│  │  ├─ base.css
│  │  ├─ layout.css             # 顶栏/侧栏/标签/分栏/状态栏
│  │  ├─ tree.css
│  │  ├─ editor.css
│  │  ├─ grid.css
│  │  ├─ dialog.css
│  │  └─ pages.css              # 欢迎页/设置页/连接页
│  ├─ vendor\codemirror\        # CM5 + sql mode + hint + 主题（含 LICENSE）
│  └─ js\
│     ├─ bridge.js              # invoke/listen/call/cancel + 浏览器 mock 回退
│     ├─ api.js                 # 命令封装 + reqId 事件分发 + 超时
│     ├─ state.js               # 全局状态 + 订阅
│     ├─ i18n.js
│     ├─ util.js
│     ├─ icons.js               # 内联 SVG 图标表
│     ├─ app.js                 # 启动/快捷键/路由
│     ├─ themes.js              # 主题应用/系统明暗监听/防闪
│     ├─ layout.js              # 侧栏/标签栏/分栏拖拽/状态栏
│     ├─ views\
│     │  ├─ welcome.js
│     │  ├─ connections.js      # 连接列表/分组/最近
│     │  ├─ tree.js             # 对象树
│     │  ├─ tabs.js
│     │  ├─ query.js            # SQL 标签页
│     │  ├─ results.js          # 多结果/消息/执行计划
│     │  ├─ table.js            # 数据标签页
│     │  ├─ design.js           # 表设计器
│     │  ├─ ddl.js
│     │  ├─ history.js
│     │  └─ settings.js
│     ├─ components\
│     │  ├─ grid.js             # 虚拟表格核心
│     │  ├─ editor.js           # CodeMirror 封装
│     │  ├─ filterbar.js
│     │  ├─ pagination.js
│     │  ├─ contextmenu.js
│     │  ├─ dialog.js
│     │  ├─ toast.js
│     │  ├─ splitter.js
│     │  └─ cellviewer.js       # 长文本/JSON/BLOB
│     └─ mock\
│        ├─ mock.js             # 浏览器开发用假后端
│        └─ fixtures.js
├─ tools\
│  ├─ contrast_check.py          # 校验 5 套主题的文本/图标对比度（CI 用）
│  ├─ make_test_db.py            # 生成 data/silkit_test.db（SQLite 夹具）
│  └─ e2e_runner.py              # E2E 入口：探测测试源 → 增量构建 → CTest → 报告
├─ tests\
│  ├─ CMakeLists.txt             # 10 个 CTest 目标（含 e2e_real_db）
│  ├─ test_main.cpp              # silkit_smoke：纯逻辑聚合
│  ├─ test_sqlite_driver.cpp / test_sqlite_fixture.cpp
│  ├─ test_engine_dialect.cpp / test_table_ops.cpp / test_ddl_builder.cpp
│  ├─ test_io.cpp / test_m8.cpp
│  ├─ integration_db.cpp         # M2/M3 冒烟 + M5/M6 真实库写路径
│  └─ e2e\
│     ├─ e2e_real_db.cpp        # 14 项 × 双引擎真实库用例（§12.4）
│     └─ seed_pg_employees.sql  # 可选：PG employees 精简镜像夹具
├─ docs\
│  ├─ dev-story.md / silkit-vs-others.md / zhihu-1.0-launch.md
└─ build\                       # 构建产物（.gitignore）
```

**命名约定**：文件/目录 `snake_case`；C++ 类型 `PascalCase`；函数/方法 `PascalCase`；成员变量 `snake_case_`；命名空间小写 `silkit::db`；常量 `kPascalCase`。注释用中文，标识符用英文（与 StepDraw 一致）。

> `HeidiSQL-12.22/` 仅作只读参考：加入 `.gitignore`（或移出代码仓库），避免误提交与被构建脚本扫描。

---

## 5. 存储与数据模型

### 5.1 数据目录

`%APPDATA%\Silkit\`：

| 文件 | 内容 | 写入时机 |
|---|---|---|
| `connections.json` | 连接配置（密码为 DPAPI 密文） | 新增/修改/删除连接、最近使用 |
| `settings.json` | 全局设置 | 设置变更（防抖 300ms） |
| `ui-state.json` | 标签页、活动连接、侧栏宽度、分栏比例、最近 SQL | 退出/每 30s 快照 |
| `history.jsonl` | 查询历史，一行一条 | 每次执行后追加；超过上限轮转 |
| `snippets.json` | SQL 片段 | 增删改 |
| `logs\silkit-YYYYMMDD.log` | 运行日志（脱敏） | 持续；5MB 轮转，保留 5 个 |
| `crash` | 崩溃转储/前端错误 | 崩溃时 |

**原子写**：写 `xxx.json.tmp` → `FlushFileBuffers` → `MoveFileExW(tmp, target, MOVEFILE_REPLACE_EXISTING)`；失败保留 `.bak`。启动时若主文件损坏，尝试 `.bak` 并提示。

### 5.2 connections.json

```json
{
  "version": 1,
  "connections": [
    {
      "id": "c_7f3a91",
      "name": "本地 MySQL",
      "engine": "mysql",
      "group": "开发",
      "color": "#4c8dff",
      "readOnly": false,
      "host": "127.0.0.1",
      "port": 3306,
      "user": "root",
      "database": "",
      "charset": "utf8mb4",
      "ssl": { "mode": "preferred", "ca": "", "cert": "", "key": "" },
      "options": { "connectTimeoutMs": 8000, "keepAlive": true, "initSql": "" },
      "secret": { "save": true, "password": "dpapi:v1:...." },
      "createdAt": "2026-11-13T10:00:00Z",
      "lastUsedAt": "2026-11-13T10:05:00Z"
    },
    {
      "id": "c_pg01",
      "name": "测试 PostgreSQL",
      "engine": "postgres",
      "group": "测试",
      "color": "#3ecf8e",
      "readOnly": true,
      "host": "10.0.0.12",
      "port": 5432,
      "user": "readonly",
      "database": "app",
      "schema": "public",
      "ssl": { "mode": "prefer" },
      "options": { "connectTimeoutMs": 8000, "applicationName": "Silkit" },
      "secret": { "save": false, "password": "" }
    },
    {
      "id": "c_sqlite01",
      "name": "本地文件 app.db",
      "engine": "sqlite",
      "color": "#f5a524",
      "readOnly": false,
      "file": "D:\\data\\app.db",
      "options": { "busyTimeoutMs": 5000, "foreignKeys": true, "journalMode": "" }
    }
  ]
}
```

**规则**
- 密码只以 `dpapi:v1:` 前缀密文出现；前端永远拿不到明文。
- `save=false` 时不写 `password`，连接时弹窗输入（仅内存保留到会话结束）。
- `readOnly=true` 时后端拒绝写语句（不仅是 UI 禁用）。
- 连接导出（v1.1）默认剥离 `secret`，提供“包含密码”二次确认。

### 5.3 settings.json

```json
{
  "version": 1,
  "ui": {
    "theme": "bloom",
    "appearance": "light",
    "followSystem": false,
    "reduceMotion": false,
    "language": "zh-CN",
    "fontSize": 13,
    "gridRowHeight": 26,
    "sidebarWidth": 264,
    "editorRatio": 0.44,
    "restoreTabs": true,
    "confirmDestructive": true
  },
  "editor": {
    "fontSize": 13,
    "tabSize": 2,
    "wordWrap": false,
    "autocomplete": true,
    "showLineNumbers": true
  },
  "query": {
    "maxRows": 5000,
    "fetchSize": 500,
    "timeoutMs": 0,
    "autoLimit": true,
    "historyLimit": 5000,
    "safeMode": true
  },
  "data": {
    "pageSize": 500,
    "nullDisplay": "NULL",
    "dateFormat": "YYYY-MM-DD HH:mm:ss",
    "binaryPreviewBytes": 65536,
    "maxCellBytes": 262144
  },
  "export": {
    "defaultEncoding": "utf-8",
    "csvDelimiter": ",",
    "includeHeader": true,
    "nullLiteral": ""
  }
}
```

### 5.4 ui-state.json（会话恢复）

```json
{
  "version": 1,
  "activeTab": "t3",
  "tabs": [
    { "kind": "query", "title": "查询 1", "connectionId": "c_7f3a91",
      "database": "app", "sql": "SELECT * FROM users LIMIT 100;", "dirty": false },
    { "kind": "table", "title": "users", "connectionId": "c_7f3a91",
      "database": "app", "schema": "", "table": "users", "page": 0, "pageSize": 500 },
    { "kind": "design", "title": "设计: orders", "connectionId": "c_7f3a91",
      "database": "app", "table": "orders" }
  ],
  "sidebar": { "expanded": ["c_7f3a91", "c_7f3a91/app", "c_7f3a91/app/tables"], "scrollTop": 120 }
}
```

- 不保存结果集（避免膨胀/过期数据），只保存 SQL 与标签页参数。
- 恢复时若连接不可用，标签页保留并显示“已断开，点击重连”。
- 未保存的查询 SQL 每 30s 写入 `ui-state.json`，崩溃后可恢复。

### 5.5 密码与 DPAPI

```cpp
// src/store/secrets.h
namespace silkit::store {
bool ProtectSecret(const std::string& plain_utf8, std::string& out_base64, std::string& err);
bool UnprotectSecret(const std::string& base64, std::string& out_plain_utf8, std::string& err);
}
```

- `CryptProtectData` + 固定 entropy `"Silkit.Connection.v1"` + `CRYPTPROTECT_UI_FORBIDDEN`。
- 密文格式：`dpapi:v1:<base64>`，便于未来迁移到主密码/AES-GCM。
- `SecureZeroMemory` 清理临时明文；日志过滤 `password`、`secret`、`token`、`authorization`。
- 前端不提供“查看密码”接口；最多显示“已保存”。

### 5.6 运行时内存模型

| 对象 | 生命周期 | 释放 |
|---|---|---|
| `Connection` | `conn.open` → `conn.close` 或退出 | Session 线程析构 |
| `Session` | 打开连接时创建 | 关闭连接/退出 |
| `ResultSet`（前端） | 标签页存活期间 | 关闭标签/重新执行 |
| 行数据 | 按批次追加，最多 `maxRows` | 标签关闭/清空 |
| 大字段 | 默认截断；按需 `cell.fetch` | 查看器关闭 |

---

## 6. 驱动抽象层

### 6.1 核心类型

```cpp
// src/db/types.h
namespace silkit::db {

enum class Engine { MySql, Postgres, Sqlite };

enum class ValueKind {
  Null, Int, UInt, Float, Decimal, Bool, Text, Binary,
  Date, Time, DateTime, Timestamp, Json, Uuid, Array, Unknown
};

struct ColumnMeta {
  std::string name;
  std::string db_type;       // 原始类型：varchar(64) / int4 / TEXT
  ValueKind kind = ValueKind::Unknown;
  bool nullable = true;
  bool primary_key = false;
  bool auto_increment = false;
  bool generated = false;
  std::string default_expr;
  std::string comment;
};

struct Cell {
  ValueKind kind = ValueKind::Null;
  bool is_null = true;
  std::string text;          // UTF-8；Binary 为 base64
  std::uint64_t bytes = 0;   // 原始字节数
  bool truncated = false;
};

struct RowBatch {
  std::vector<std::vector<Cell>> rows;
};

using RowSink = std::function<bool(const RowBatch&)>;  // 返回 false = 停止

struct Param {
  ValueKind kind = ValueKind::Text;
  bool is_null = false;
  std::string text;          // Binary 为 base64
};

struct ExecOptions {
  std::size_t fetch_size = 500;
  std::size_t max_rows = 0;  // 0 = 不限
  std::uint64_t timeout_ms = 0;
};

struct DbError {
  int code = 0;
  std::string sql_state;
  std::string message;
  std::string detail;
  std::string hint;
  int position = -1;         // 语句内字符偏移（1-based），未知为 -1
};

struct ExecResult {
  std::vector<ColumnMeta> columns;
  std::uint64_t rows = 0;
  std::uint64_t affected = 0;
  std::uint64_t last_insert_id = 0;
  std::string info;          // 服务端消息（warning 数等）
  std::uint64_t elapsed_ms = 0;
  bool cancelled = false;
  bool truncated = false;
  DbError error;             // error.code == 0 表示成功
};

}  // namespace silkit::db
```

### 6.2 连接与驱动接口

```cpp
// src/db/driver.h
namespace silkit::db {

struct Capabilities {
  bool databases = false;        // 多数据库
  bool schemas = false;          // 多 schema
  bool views = true;
  bool routines = false;
  bool triggers = true;
  bool events = false;
  bool sequences = false;
  bool transactional_ddl = false;
  bool explain = true;
  bool cancel = true;
  bool user_management = false;
  bool table_comment = false;
};

class Connection {
public:
  virtual ~Connection() = default;
  virtual Engine EngineKind() const = 0;
  virtual Capabilities Caps() const = 0;

  virtual bool Ping(DbError& err) = 0;
  virtual ExecResult Execute(const std::string& sql,
                             const std::vector<Param>& params,
                             const ExecOptions& opt,
                             const RowSink& sink) = 0;
  virtual void Cancel() = 0;  // 必须线程安全，可从任意线程调用

  virtual std::string ServerVersion() const = 0;
  virtual std::string CurrentDatabase() const = 0;
  virtual std::uint64_t ThreadId() const = 0;  // MySQL thread id / PG backend pid
  virtual std::string Placeholder(std::size_t index) const = 0;  // "?" / "$1"
};

struct ConnectParams {
  Engine engine = Engine::Sqlite;
  std::string host, port, user, password, database, schema, file, charset;
  std::string ssl_mode;       // disable/allow/prefer/require/verify-ca/verify-full
  std::string ssl_ca, ssl_cert, ssl_key;
  bool read_only = false;
  int connect_timeout_ms = 8000;
  std::string application_name = "Silkit";
  std::string init_sql;
};

class Driver {
public:
  virtual ~Driver() = default;
  virtual Engine EngineKind() const = 0;
  virtual std::unique_ptr<Connection> Open(const ConnectParams& p, DbError& err) = 0;
};

}  // namespace silkit::db
```

**关键约束**
- `Execute` 只允许在 Session 线程调用；`Cancel` 可从任意线程调用。
- 不抛异常；所有失败写入 `DbError`；`ExecResult::error.code==0` 表示成功。
- `sink` 返回 `false` 时驱动尽快停止（用于 `max_rows` 或用户取消）。
- 参数一律使用驱动原生占位符与绑定，不做字符串拼接。

### 6.3 类型映射

| UI 类型 | MySQL / MariaDB | PostgreSQL | SQLite |
|---|---|---|---|
| Int | tinyint/smallint/mediumint/int/bigint | int2/int4/int8/serial/bigserial | INTEGER（亲和） |
| UInt | unsigned int 系列 | — | — |
| Float | float/double | float4/float8 | REAL |
| Decimal | decimal/numeric | numeric | NUMERIC |
| Bool | tinyint(1)、bit(1) | boolean | INTEGER 0/1（按声明） |
| Text | char/varchar/text/enum/set | char/varchar/text/name/citext | TEXT |
| Binary | binary/varbinary/blob/bit | bytea | BLOB |
| Date/Time | date/time/datetime/timestamp/year | date/time/timestamp/timestamptz/interval | 按声明类型 |
| Json | json | json/jsonb | TEXT（json1） |
| Uuid | char(36)/binary(16) | uuid | TEXT |
| Array | — | 任意数组类型 | — |
| Unknown | 其余 | 其余 | 其余 |

- 类型识别只用于**选择编辑器/对齐/复制格式**，不改变原始文本。
- 显示保留服务端文本（时间戳不擅自转时区）；状态栏可显示会话时区。
- `tinyint(1)` 在 MySQL 中可能是布尔也可能是数字，UI 提供“按布尔显示”开关（默认按类型名）。

### 6.4 能力矩阵（v1 目标）

| 能力 | MySQL | PostgreSQL | SQLite |
|---|---|---|---|
| 多数据库 | ✅ database | ✅ database | ✅ ATTACH（只读浏览） |
| Schema | ❌（database 即 schema） | ✅ | ❌ |
| 表/视图 | ✅ | ✅（含物化视图） | ✅ |
| 函数/过程 | ✅ | ✅ | ❌ |
| 触发器 | ✅ | ✅ | ✅ |
| 事件 | ✅ | ❌ | ❌ |
| 序列 | ❌（AUTO_INCREMENT） | ✅ | ❌ |
| 事务 DDL | ❌（隐式提交） | ✅ | ⚠️ 部分 |
| 取消查询 | ✅ KILL QUERY | ✅ PQcancel | ✅ sqlite3_interrupt |
| EXPLAIN | ✅ | ✅（JSON） | ✅ QUERY PLAN |
| 用户管理 | ⚠️ v1.1 | ⚠️ v1.1 | ❌ |

### 6.5 错误模型

- 统一 `DbError{code, sql_state, message, detail, hint, position}`。
- MySQL：`mysql_errno` / `mysql_sqlstate` / `mysql_error`；解析 `position` 较难，默认 -1。
- PG：`PQresultErrorField` 的 `PG_DIAG_*`（SQLSTATE、MESSAGE_PRIMARY、DETAIL、HINT、STATEMENT_POSITION）。
- SQLite：`sqlite3_extended_errcode` / `sqlite3_errmsg`；`sqlite3_error_offset`（3.38+）。
- 多语句：错误附带 `statementIndex` 与语句起始偏移，前端换算到编辑器行列并高亮。

### 6.6 SQL 分割器

`sql_splitter` 是查询工作区的基础，必须有单元测试。支持：

| 场景 | 处理 |
|---|---|
| `'...'`、`"..."`、`` `...` `` | 引号内的 `;` 不切分；处理 `''`/`""`/`` `` `` 转义 |
| MySQL 反斜杠转义 | `\'`、`\\`（默认开启，SQLite/PG 不开） |
| `--` 行注释、`#`（MySQL）、`/* ... */` | 注释内不切分 |
| `/*! ... */` | 保留为语句内容（MySQL 版本注释） |
| PG `$tag$ ... $tag$` | dollar-quoted 内不切分（函数体） |
| MySQL `DELIMITER $$` | 识别并切换分隔符（存储过程脚本） |
| 空语句/纯注释 | 跳过，但保留偏移 |
| 末尾无分号 | 作为最后一条语句 |

输出 `Statement{text, begin_offset, end_offset}`；前端用偏移做错误定位，后端按顺序执行。

### 6.7 结果流式、行数上限与取消

- **分批**：`fetch_size` 默认 500 行或 1MB 文本，先到者触发；每批一条 `ipc.event`（`type=batch`）。
- **上限**：默认 `maxRows=5000`；达到后设置 `truncated=true`，UI 显示“已显示前 5000 行，继续加载 / 导出全部”。
  - MySQL：`mysql_use_result` 逐行读；停止时 `mysql_free_result` 排空剩余（必要时标记会话需重置）。
  - PG：`PQsendQuery` + `PQsetSingleRowMode`；停止时继续 `PQgetResult` 排空或取消。
  - SQLite：`sqlite3_step` 循环；停止时 `sqlite3_reset`。
- **取消**：
  - UI 发 `ipc.cancel {reqId}` → AsyncRouter 标记 → `Connection::Cancel()`。
  - MySQL：独立控制连接执行 `KILL QUERY <thread_id>`；`thread_id` 来自 `mysql_thread_id`。
  - PG：`PQcancel`（libpq 允许跨线程调用）。
  - SQLite：`sqlite3_interrupt`。
  - 取消后连接可能处于“结果未排空”状态：先尝试排空；失败则关闭并提示“连接已重置”。
- **背压**：前端每收到 N 批回 `query.ack {reqId, seq}`；未确认批数超过 8 时驱动线程等待（条件变量），防止 UI 线程事件队列爆炸。

### 6.8 会话生命周期

```
conn.open → 解密密码 → Driver::Open → Session 线程启动 → meta 根节点
   │
   ├─ 正常查询/元数据/事务
   ├─ 连接丢失（ping 失败）→ status=disconnected → UI 显示重连按钮
   ├─ conn.reconnect → 关闭旧连接，重新 Open；子标签页保留
   └─ conn.close → 取消当前任务 → 关闭 → join → 释放元数据缓存
```

**事务状态**：Session 维护 `autocommit` 与 `in_transaction`；在状态栏显示。用户切换连接/关闭标签不自动提交，提示“有未提交事务”。

---

## 7. 驱动实现要点

### 7.1 MySQL / MariaDB（libmariadb）

**依赖**：vcpkg `libmariadb`（LGPL-2.1），头文件 `mysql.h`，链接 `unofficial::libmariadb`。

**连接参数**
| 参数 | 说明 |
|---|---|
| host/port | IPv4/IPv6/主机名；默认 3306 |
| user/password | 不写入日志 |
| database | 可空；连接后再选库 |
| charset | 默认 `utf8mb4`；连接时 `mysql_options(MYSQL_SET_CHARSET_NAME)` |
| ssl_mode | `disable/preferred/required/verify-ca/verify-identity` → `mysql_ssl_set` + `MYSQL_OPT_SSL_ENFORCE`/`MYSQL_OPT_SSL_VERIFY_SERVER_CERT`（按 Connector/C 版本能力探测） |
| connect_timeout | `MYSQL_OPT_CONNECT_TIMEOUT` |
| read/write timeout | `MYSQL_OPT_READ_TIMEOUT` / `MYSQL_OPT_WRITE_TIMEOUT` |
| init_sql | 连接成功后执行（分号拆分） |

**实现要点**
- `mysql_init` → `mysql_real_connect`；失败取 `mysql_errno/mysql_error`。
- 文本协议读：`mysql_real_query` + `mysql_use_result`（流式）逐行 `mysql_fetch_row`/`mysql_fetch_lengths`。
- 写/参数化：`mysql_stmt_prepare` + `MYSQL_BIND`；`Param` → `MYSQL_TYPE_STRING/BLOB/LONGLONG/DOUBLE/NULL`。
- 多结果集：本工具自己拆分语句，关闭 `CLIENT_MULTI_STATEMENTS`（降低注入面）。
- 切库：`USE \`db\``（会话级）；跨库查询用全限定名；切库后刷新树与当前库状态。
- 元数据：`mysql_fetch_fields` 取 `MYSQL_FIELD`（name/type/flags/length/decimals）。
- 取消：懒创建一条控制连接，`KILL QUERY <thread_id>`；控制连接失败则退化为关闭主连接。
- `read_only`：连接后 `SET SESSION TRANSACTION READ ONLY`；同时在语句层拦截非 SELECT。
- 自动重连：**关闭**（`MYSQL_OPT_RECONNECT=0`），由 UI 显式重连，避免事务状态丢失。
- 事务：`START TRANSACTION` / `COMMIT` / `ROLLBACK`；`autocommit` 通过 `SET autocommit=0/1`。
- 字符集：统一 UTF-8 传输；`utf8mb4` 覆盖 emoji；`binary` 走 base64。
- 类型细节：`BIT` 返回原始字节；`DECIMAL` 保持字符串；`DATETIME(6)` 保留微秒；`JSON` 按文本。

**元数据 SQL（参数化）**

```sql
-- 数据库
SELECT SCHEMA_NAME, DEFAULT_CHARACTER_SET_NAME, DEFAULT_COLLATION_NAME
FROM information_schema.SCHEMATA ORDER BY SCHEMA_NAME;

-- 表/视图
SELECT TABLE_NAME, TABLE_TYPE, ENGINE, TABLE_ROWS, DATA_LENGTH, INDEX_LENGTH,
       TABLE_COLLATION, TABLE_COMMENT, CREATE_TIME, UPDATE_TIME
FROM information_schema.TABLES
WHERE TABLE_SCHEMA = ? AND TABLE_TYPE IN ('BASE TABLE','VIEW','SYSTEM VIEW')
ORDER BY TABLE_NAME;

-- 列
SELECT COLUMN_NAME, ORDINAL_POSITION, COLUMN_DEFAULT, IS_NULLABLE,
       DATA_TYPE, COLUMN_TYPE, CHARACTER_MAXIMUM_LENGTH, NUMERIC_PRECISION,
       NUMERIC_SCALE, DATETIME_PRECISION, COLUMN_KEY, EXTRA, COLUMN_COMMENT,
       COLLATION_NAME, GENERATION_EXPRESSION
FROM information_schema.COLUMNS
WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ?
ORDER BY ORDINAL_POSITION;

-- 索引
SELECT INDEX_NAME, NON_UNIQUE, SEQ_IN_INDEX, COLUMN_NAME, COLLATION,
       CARDINALITY, INDEX_TYPE, INDEX_COMMENT
FROM information_schema.STATISTICS
WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ?
ORDER BY INDEX_NAME, SEQ_IN_INDEX;

-- 外键
SELECT k.CONSTRAINT_NAME, k.COLUMN_NAME, k.REFERENCED_TABLE_SCHEMA,
       k.REFERENCED_TABLE_NAME, k.REFERENCED_COLUMN_NAME,
       r.UPDATE_RULE, r.DELETE_RULE, k.ORDINAL_POSITION
FROM information_schema.KEY_COLUMN_USAGE k
JOIN information_schema.REFERENTIAL_CONSTRAINTS r
  ON r.CONSTRAINT_SCHEMA = k.CONSTRAINT_SCHEMA
 AND r.CONSTRAINT_NAME = k.CONSTRAINT_NAME
WHERE k.TABLE_SCHEMA = ? AND k.TABLE_NAME = ?
  AND k.REFERENCED_TABLE_NAME IS NOT NULL
ORDER BY k.CONSTRAINT_NAME, k.ORDINAL_POSITION;
```

- 函数/过程：`information_schema.ROUTINES`；触发器：`information_schema.TRIGGERS`；事件：`information_schema.EVENTS`。
- DDL：`SHOW CREATE TABLE`、`SHOW CREATE VIEW`、`SHOW CREATE PROCEDURE/FUNCTION`、`SHOW CREATE TRIGGER`、`SHOW CREATE EVENT`。
- 行数估算：`TABLE_ROWS`（InnoDB 近似）；“精确计数”按钮另行 `SELECT COUNT(*)`。

**已知限制**
- MySQL DDL 隐式提交：表设计器必须明确警告“无法回滚”。
- `utf8mb3` 旧库可能丢 emoji：连接后检测并提示。
- MySQL 8.4 默认禁用 `mysql_native_password`：libmariadb 支持 `caching_sha2_password`，需在 M2 实测无 SSL 场景。
- `BIT`/`BINARY` 按 base64 展示，编辑时按十六进制输入。

### 7.2 PostgreSQL（libpq）

**依赖**：vcpkg `libpq`（PostgreSQL License），`find_package(PostgreSQL REQUIRED)`，链接 `PostgreSQL::PostgreSQL`（传递 OpenSSL/zlib/lz4）。

**连接参数**
| 参数 | 说明 |
|---|---|
| host/port | 默认 5432；支持 Unix socket 路径（v1.1） |
| user/password/database | `PQconnectdbParams` |
| sslmode | `disable/allow/prefer/require/verify-ca/verify-full` |
| connect_timeout | 秒；`connect_timeout` |
| application_name | `Silkit` |
| schema | 连接后 `SET search_path TO ...` |
| options | `client_encoding=UTF8`，`bytea_output=hex` |

**实现要点**
- `PQconnectdbParams` → 检查 `CONNECTION_OK`；失败取 `PQerrorMessage`。
- 查询：`PQsendQuery` + `PQsetSingleRowMode` + 循环 `PQgetResult`/`PQgetvalue`；单行模式下每次 `PQgetResult` 返回一行结果，`PQresultStatus` 区分 `PGRES_TUPLES_OK`/`PGRES_COMMAND_OK`。
- 参数化：`PQexecParams`（文本参数，`paramTypes=nullptr`）；二进制用 `\x...` 文本或 `paramFormats`。
- 取消：`PQcancel`（跨线程）；`PQgetCancel` 对象按连接持有。
- 元数据：`PQfname`、`PQftype`、`PQftypmod`、`PQfsize`、`PQfnumber`；类型名通过 `format_type` 查询补齐。
- 事务：`BEGIN`/`COMMIT`/`ROLLBACK`；隔离级别 `SET TRANSACTION ISOLATION LEVEL ...`；只读 `SET TRANSACTION READ ONLY`。
- 多数据库：PG 连接绑定单库；切库 = 新建连接（UI 树中“数据库”节点展开时提示）。
- 大字段：`bytea` 按 hex 解码为 base64；`json/jsonb` 原样文本；数组原样文本（v1.1 提供展开）。
- 时间：保留服务端文本；`timestamptz` 显示原值，状态栏标注会话 `TimeZone`。

**元数据 SQL**

```sql
-- 数据库
SELECT datname FROM pg_database
WHERE NOT datistemplate AND datallowconn ORDER BY datname;

-- schema
SELECT nspname FROM pg_namespace
WHERE nspname NOT IN ('pg_catalog','information_schema','pg_toast')
  AND nspname NOT LIKE 'pg_temp_%' AND nspname NOT LIKE 'pg_toast_temp_%'
ORDER BY nspname;

-- 表/视图/物化视图/外部表
SELECT n.nspname AS schema, c.relname AS name,
       CASE c.relkind WHEN 'r' THEN 'table' WHEN 'p' THEN 'partitioned_table'
                      WHEN 'v' THEN 'view' WHEN 'm' THEN 'materialized_view'
                      WHEN 'f' THEN 'foreign_table' ELSE c.relkind::text END AS kind,
       pg_total_relation_size(c.oid) AS total_bytes,
       c.reltuples::bigint AS est_rows,
       obj_description(c.oid, 'pg_class') AS comment
FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace
WHERE n.nspname = $1 AND c.relkind IN ('r','p','v','m','f')
ORDER BY c.relname;

-- 列
SELECT a.attname AS name,
       format_type(a.atttypid, a.atttypmod) AS data_type,
       a.attnotnull AS not_null,
       pg_get_expr(d.adbin, d.adrelid) AS default_expr,
       a.attnum AS ordinal,
       col_description(a.attrelid, a.attnum) AS comment,
       a.attidentity AS identity,
       a.attgenerated AS generated
FROM pg_attribute a
LEFT JOIN pg_attrdef d ON d.adrelid = a.attrelid AND d.adnum = a.attnum
WHERE a.attrelid = $1::regclass AND a.attnum > 0 AND NOT a.attisdropped
ORDER BY a.attnum;

-- 索引/约束
SELECT i.relname AS index_name, ix.indisunique, ix.indisprimary,
       pg_get_indexdef(ix.indexrelid) AS definition
FROM pg_index ix JOIN pg_class i ON i.oid = ix.indexrelid
WHERE ix.indrelid = $1::regclass ORDER BY i.relname;

SELECT conname, contype, pg_get_constraintdef(oid) AS definition
FROM pg_constraint WHERE conrelid = $1::regclass
ORDER BY contype, conname;
```

- 视图定义：`pg_get_viewdef($1::regclass, true)`。
- 函数：`pg_proc` + `pg_get_functiondef(oid)`；触发器：`pg_trigger` + `pg_get_triggerdef`。
- 序列：`information_schema.sequences`。
- DDL 组装：**PG 没有内置 `SHOW CREATE TABLE`**，由 `ddl_builder` 根据列/默认值/约束/索引/注释拼装；明确标注“由目录重建，可能省略权限、表空间、分区、继承、RLS 等”。
- `search_path`：树中 schema 限定；生成 SQL 时除 `public` 外默认带 schema 前缀。

**已知限制**
- 跨库查询需 `dblink`/`postgres_fdw`，本工具不做。
- 分区表显示为 `partitioned_table`，子分区 v1.1 展开。
- 大对象（`lo`）不在 v1 支持，显示 OID。

### 7.3 SQLite（sqlite3）

**依赖**：vcpkg `sqlite3`，`find_package(unofficial-sqlite3 CONFIG REQUIRED)`，链接 `unofficial::sqlite3::sqlite3`。

**连接参数**
| 参数 | 说明 |
|---|---|
| file | 绝对路径；不存在时按“新建”处理（需确认） |
| read_only | `SQLITE_OPEN_READONLY`，否则 `READWRITE\|CREATE` |
| busy_timeout | 默认 5000ms |
| foreign_keys | 默认 ON |
| journal_mode | 可选 `WAL`；切换前提示 |

**实现要点**
- `sqlite3_open_v2` + `sqlite3_extended_result_codes(1)` + `sqlite3_busy_timeout`。
- 执行：`sqlite3_prepare_v2` → `sqlite3_bind_*` → `sqlite3_step` 循环；`SQLITE_ROW` 取值，`SQLITE_DONE` 结束。
- 取消：`sqlite3_interrupt`（跨线程安全）。
- 类型：SQLite 动态类型；按 `sqlite3_column_type` 判定实际值，列声明类型仅作提示。
- 元数据：
  ```sql
  PRAGMA database_list;                    -- main / attached
  SELECT name, type, sql FROM sqlite_master
  WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%' ORDER BY name;
  PRAGMA table_xinfo('t');                 -- 含隐藏列
  PRAGMA index_list('t'); PRAGMA index_info('idx');
  PRAGMA foreign_key_list('t');
  SELECT name, sql FROM sqlite_master WHERE type='trigger' AND tbl_name=?;
  ```
- **PRAGMA 注意**：`PRAGMA table_xinfo('t')` 不支持参数绑定，表名必须来自已加载元数据并用 `QuoteIdent` 拼接；禁止直接使用用户输入。
- DDL：直接取 `sqlite_master.sql`；表设计器生成 `ALTER TABLE`。
- 行标识：优先主键；否则 `rowid`（`WITHOUT ROWID` 表无 rowid → 只读）。检测：`PRAGMA table_list` 或 `SELECT rowid FROM t LIMIT 1`。
- 事务：`BEGIN IMMEDIATE`/`COMMIT`/`ROLLBACK`；保存点用于批量导入。
- ATTACH 数据库：树中显示为一级节点；只读浏览，写入按 SQLite 规则。
- 只读：`SQLITE_OPEN_READONLY` + 语句拦截。

**已知限制**
- 无用户/权限/存储过程。
- `ALTER TABLE` 能力有限：v1 支持 ADD COLUMN、RENAME COLUMN（3.25+）、DROP COLUMN（3.35+）、RENAME TABLE；复杂类型变更/约束变更提示“需重建表（v1.1）”。
- 并发写：WAL 下仍可能 `SQLITE_BUSY`；重试 3 次并提示。

### 7.4 版本兼容策略

- **最低支持**：MySQL 5.7 / MariaDB 10.5、PostgreSQL 12、SQLite 3.35。
- 不靠版本号硬编码，优先**能力探测**：
  - MySQL：`SELECT VERSION()` + 编译期 `MARIADB_VERSION_ID`；`information_schema` 列存在性。
  - PG：`PQserverVersion` + `pg_catalog` 查询；`pg_get_functiondef` 等函数存在性。
  - SQLite：`sqlite3_libversion_number()`。
- 新特性（如 PG 16 的 `pg_stat_io`）只在 v1.1 的监控里使用。

---

## 8. IPC 契约

### 8.1 为什么需要异步层

TauriCPP 的 `Bridge::HandleInvoke` 在 **UI 线程同步执行**。直接注册 `conn.open` 并查库会让窗口在连接超时/慢查询时假死。因此：

- 所有数据库/文件 IO 命令统一走 `ipc.call`；
- `ipc.call` **立即返回** `{ok:true, reqId}`；
- 结果通过 `ipc.event` 事件推送；
- `ipc.cancel` 取消；
- 只有 `app.*`/`settings.get` 等毫秒级命令走同步 `invoke`。

### 8.2 调用协议

```js
// frontend/js/bridge.js
const { reqId } = await Bridge.invoke('ipc.call', { cmd: 'query.execute', args: {...} });
const off = Bridge.listen('ipc.event', (e) => {
  if (e.reqId !== reqId) return;
  if (e.type === 'batch')   onBatch(e.data);
  if (e.type === 'progress') onProgress(e.data);
  if (e.type === 'result')  { onDone(e.data); off(); }
  if (e.type === 'error')   { onError(e.data); off(); }
});
// 取消
await Bridge.invoke('ipc.cancel', { reqId });
```

C++ 侧：

```cpp
// ipc/async_router.h
struct IpcRequest {
  std::string req_id;
  std::string cmd;
  nlohmann::json args;
  std::atomic<bool> cancelled{false};
};

class AsyncRouter {
public:
  using Handler = std::function<void(IpcRequest&, IpcResponder&)>;
  void Register(const std::string& cmd, Handler h);
  nlohmann::json Call(const nlohmann::json& args);   // 返回 {ok, reqId}
  nlohmann::json Cancel(const nlohmann::json& args); // 返回 {ok}
  void Shutdown();
private:
  ThreadPool pool_{4};
  std::unordered_map<std::string, std::shared_ptr<IpcRequest>> inflight_;
};
```

`IpcResponder` 封装 `Bridge::Emit("ipc.event", ...)`，负责：
- `Batch(reqId, seq, columns?, rows)`；
- `Progress(reqId, current, total, message)`；
- `Result(reqId, data)`；
- `Error(reqId, DbError)`；
- 节流：batch 最多 20 条/秒或 500 行/条；超过则合并。

### 8.3 事件格式

```json
{
  "reqId": "r_01H...",
  "type": "batch",
  "seq": 12,
  "data": {
    "statement": 0,
    "columns": [{ "name": "id", "type": "int8", "kind": "int" }],
    "rows": [["1", "alice", null]]
  }
}
```

```json
{
  "reqId": "r_01H...",
  "type": "result",
  "data": {
    "statements": [
      { "index": 0, "rows": 42, "affected": 0, "elapsedMs": 12,
        "truncated": false, "columns": 4 }
    ],
    "elapsedMs": 14
  }
}
```

```json
{
  "reqId": "r_01H...",
  "type": "error",
  "data": {
    "code": 1064,
    "sqlState": "42000",
    "message": "You have an error in your SQL syntax...",
    "statementIndex": 0,
    "position": 15
  }
}
```

### 8.4 错误返回约定

- 同步命令：`{ok:false, error:{code,message,...}}`。
- 异步命令：`ipc.event type=error`，`reqId` 对应。
- 未知命令：`{ok:false, error:{code:-32601, message:"unknown command"}}`。
- 参数错误：`code:-32602`，附 `field`。
- 超时：`code:-32000`，前端显示“操作超时，可取消或重试”。

### 8.5 命令清单

> 命名：`域.动作`。DB 命令全部走 `ipc.call`，下表“异步”列标注。

#### app / settings / ui

| 命令 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `app.info` | — | `{version, build, engines, platform}` | 同步 |
| `app.ready` | — | `{ok}` | 前端首帧完成 |
| `app.quit` | — | `{ok}` | 触发关闭流程 |
| `app.log` | `{level, message}` | `{ok}` | 前端错误/诊断 |
| `app.open_url` | `{url}` | `{ok}` | 仅 http/https |
| `app.pick_file` | `{kind:"sqlite"\|"sql"\|"csv", save?, defaultName?}` | `{path}` | 文件对话框 |
| `settings.get` | — | `{settings}` | 同步 |
| `settings.set` | `{patch}` | `{settings}` | 防抖写入 |
| `ui_state.get` | — | `{state}` | 同步 |
| `ui_state.set` | `{state}` | `{ok}` | 退出/30s 快照 |
| `theme.set` | `{theme, appearance}` | `{ok, windowBg}` | 切换主题调色板与明暗；同步窗口底色，避免白闪 |

#### 连接

| 命令 | 参数 | 返回 | 异步 |
|---|---|---|---|
| `conn.list` | — | `{connections}`（无密码） | 否 |
| `conn.save` | `{profile, password?}` | `{id}` | 否 |
| `conn.delete` | `{id}` | `{ok}` | 否 |
| `conn.test` | `{profile, password?}` | `{ok, serverVersion, elapsedMs, error?}` | 是 |
| `conn.open` | `{id, password?}` | `{sessionId, serverVersion, currentDb, capabilities}` | 是 |
| `conn.close` | `{sessionId}` | `{ok}` | 是 |
| `conn.reconnect` | `{sessionId}` | `{sessionId, serverVersion}` | 是 |
| `conn.ping` | `{sessionId}` | `{ok, latencyMs}` | 是 |
| `conn.set_database` | `{sessionId, database}` | `{currentDb}` | 是 |
| `conn.set_readonly` | `{sessionId, readOnly}` | `{ok}` | 是 |
| `conn.stats` | `{sessionId}` | `{threadId, version, timezone, charset, tx}` | 是 |

#### 元数据

| 命令 | 参数 | 返回 | 异步 |
|---|---|---|---|
| `meta.children` | `{sessionId, parent:{kind,database,schema,name}, refresh?}` | `{nodes:[...]}` | 是 |
| `meta.detail` | `{sessionId, ref}` | `{columns,indexes,fks,triggers,comment,...}` | 是 |
| `meta.ddl` | `{sessionId, ref}` | `{sql, warnings}` | 是 |
| `meta.search` | `{sessionId, keyword, kinds?, limit?}` | `{nodes}` | 是 |
| `meta.refresh` | `{sessionId, ref}` | `{ok}` | 是 |

**节点结构**

```json
{
  "kind": "table",
  "name": "users",
  "database": "app",
  "schema": "public",
  "comment": "用户表",
  "badge": "1.2k",
  "hasChildren": true,
  "expandable": true
}
```

`kind`：`connection|database|schema|folder|table|view|routine|trigger|event|sequence|index`。
`folder` 的 `name`：`tables|views|routines|triggers|events|sequences|indexes`。

#### 查询

| 命令 | 参数 | 返回/事件 | 异步 |
|---|---|---|---|
| `query.execute` | `{sessionId, sql, database?, schema?, maxRows?, fetchSize?, continueOnError?}` | `ipc.event`：`type=batch/result/error` | 是 |
| `query.cancel` | `{sessionId}` | `{ok}` | 否 |
| `query.explain` | `{sessionId, sql, analyze?, database?, schema?}` | `{format:"text"\|"json", plan}` | 是 |
| `query.history.list` | `{sessionId?, keyword?, limit?, offset?}` | `{items,total}` | 否 |
| `query.history.clear` | `{sessionId?}` | `{ok}` | 否 |
| `query.snippet.list` | — | `{snippets}` | 否 |
| `query.snippet.save` | `{snippet}` | `{id}` | 否 |
| `query.snippet.delete` | `{id}` | `{ok}` | 否 |
| `query.save_file` | `{path?, sql, encoding?}` | `{path}` | 否 |
| `query.open_file` | `{path}` | `{sql, encoding, path}` | 否 |

#### 表数据

| 命令 | 参数 | 返回 | 异步 |
|---|---|---|---|
| `table.rows` | `{sessionId, ref, page, pageSize, order?, filters?, search?}` | `{columns, rows, page, hasMore, totalEstimate, identity, editable, readOnlyReason}` | 是 |
| `table.count` | `{sessionId, ref, filters?, search?}` | `{count}` | 是 |
| `table.apply` | `{sessionId, ref, changes, dryRun?}` | `{sqlPreview, affected, errors?}` | 是 |
| `table.truncate` | `{sessionId, ref, confirmName}` | `{ok}` | 是 |
| `table.cell` | `{sessionId, ref, identity, column}` | `{kind, text, bytes, truncated}` | 是 |

**changes 结构**

```json
{
  "inserts": [{ "values": { "name": "alice", "age": "18" } }],
  "updates": [{ "keys": { "id": "1" }, "changes": { "name": "bob" } }],
  "deletes": [{ "keys": { "id": "2" } }]
}
```

#### DDL / 表设计

| 命令 | 参数 | 返回 | 异步 |
|---|---|---|---|
| `ddl.preview` | `{sessionId, action:"create"\|"alter"\|"drop"\|"rename", ref?, def}` | `{statements, warnings}` | 是 |
| `ddl.apply` | `{sessionId, statements, confirmToken?}` | `{applied, errors?}` | 是 |
| `ddl.drop` | `{sessionId, ref, confirmName}` | `{ok}` | 是 |
| `ddl.rename` | `{sessionId, ref, newName}` | `{ok}` | 是 |
| `ddl.create_database` | `{sessionId, name, options?}` | `{ok}` | 是 |

#### 导入 / 导出 / 转储

| 命令 | 参数 | 返回/事件 | 异步 |
|---|---|---|---|
| `io.export.start` | `{sessionId, source, format, path, options}` | `io.progress` / `io.result` | 是 |
| `io.export.cancel` | `{jobId}` | `{ok}` | 否 |
| `io.import.preview` | `{path, options}` | `{columns, rows, detected}` | 是 |
| `io.import.start` | `{sessionId, target, path, mapping, options}` | `io.progress` / `io.result` | 是 |
| `io.dump.start` | `{sessionId, scope, path, options}` | `io.progress` / `io.result` | 是 |
| `io.sqlfile.run` | `{sessionId, path, options}` | `query.*`（复用查询事件） | 是 |
| `io.job.cancel` | `{jobId}` | `{ok}` | 否 |

### 8.6 关键示例

**conn.test**

```json
// 请求
{ "cmd": "conn.test",
  "args": { "profile": { "engine": "mysql", "host": "127.0.0.1", "port": 3306,
            "user": "root", "database": "" }, "password": "***" } }
// 结果事件
{ "reqId": "r1", "type": "result",
  "data": { "ok": true, "serverVersion": "8.4.3", "elapsedMs": 38 } }
```

**query.execute（流式）**

```json
{ "cmd": "query.execute",
  "args": { "sessionId": "s1", "sql": "SELECT id,name FROM users ORDER BY id",
            "maxRows": 5000, "fetchSize": 500 } }
```

事件序列（均通过 `ipc.event`，带 `statement` 与 `seq`）：
1. `type=batch` 且 `columns` 非空（首批发列元数据）
2. `type=batch`（后续行批次）
3. … 直到 `maxRows`
4. `type=result {statements:[{rows:5000,truncated:true,elapsedMs:31}], elapsedMs:32}`

**table.apply（先预览）**

```json
// dryRun=true → 只返回 SQL，不执行
{ "cmd": "table.apply",
  "args": { "sessionId":"s1",
            "ref":{"database":"app","schema":"","name":"users"},
            "dryRun":true,
            "changes":{ "updates":[{"keys":{"id":"1"},"changes":{"name":"bob"}}] } } }
```

返回：
```json
{ "sqlPreview": ["UPDATE `users` SET `name` = ? WHERE `id` = ?"],
  "paramPreview": [["bob","1"]], "affected": 0 }
```

### 8.7 契约约定

1. **密码永不回传**：`conn.list`/`conn.save` 的响应不允许出现明文或密文。
2. **所有标识符来自元数据**：表设计器/筛选器只接受已加载对象的名称；拼接前按引擎引用，拒绝控制字符。
3. **所有值走参数**：`table.apply`、导入、筛选值必须参数化。
4. **事件顺序**：`result`/`error` 必为最后一个事件；之后不再发同 reqId 事件。
5. **reqId 唯一**：`r_<ULID>`；前端维护 pending map，超时 30s 清理。
6. **关闭连接**：`conn.close` 先取消在途任务，再发 `session.closed` 事件，前端关闭相关标签或标记断开。
7. **兼容性**：命令参数只增不改；废弃字段保留一个版本并在本文标注。

---

## 9. 前端设计

### 9.1 布局

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ 顶栏 44px  Logo Silkit │ 新建连接 新建查询 打开SQL │ 最近 │ 主题·晨樱 ▾ │
├───────────────┬──────────────────────────────────────────────────────────────┤
│ 侧栏 264px    │ 标签栏 36px  [查询 1 ×][表: users ×][设计: orders ×]        │
│ ┌───────────┐ ├──────────────────────────────────────────────────────────────┤
│ │ 搜索对象…  │ │ 查询编辑器（可拖拽高度，默认 44%）                           │
│ ├───────────┤ │  1  SELECT u.id, u.name                                      │
│ │▾ 本地 MySQL│ │  2  FROM users u                                             │
│ │  ▾ app     │ │  3  WHERE u.created_at > '2026-01-01';                       │
│ │   ▾ 表     │ ├──────────────────────────────────────────────────────────────┤
│ │     users  │ │ [结果 1][结果 2][消息][执行计划]        [导出][复制][筛选]  │
│ │     orders │ │ ┌────┬────────┬──────────────┬───────────┐                  │
│ │   ▸ 视图   │ │ │ id │ name   │ email        │ created_at│                  │
│ │   ▸ 函数   │ │ ├────┼────────┼──────────────┼───────────┤                  │
│ │▸ 测试 PG   │ │ │ 1  │ alice  │ a@example.com│ 2026-…    │                  │
│ └───────────┘ │ └────┴────────┴──────────────┴───────────┘                  │
├───────────────┴──────────────────────────────────────────────────────────────┤
│ 状态栏 24px  ●本地MySQL │ MySQL 8.4.3 │ app │ 自动提交 │ 12ms │ 42 行 │ UTF-8 │ v1.0.4 │
└──────────────────────────────────────────────────────────────────────────────┘
```

- 侧栏可折叠（`Ctrl+B`）；编辑器/结果分栏可拖拽；标签栏溢出横向滚动。
- 欢迎页占据主区域：最近连接卡片、快速新建、打开 SQLite 文件、最近 SQL、快捷键提示。

### 9.2 视觉规范（时尚配色，默认偏女性向）

**设计取向**：低饱和玫瑰 / 梅子 / 薰衣草为主调，柔和阴影、较大圆角、克制的粉色面积；既有时装杂志的轻盈感，又保证长时间看数据不刺眼、不丢失专业感。

**默认外观**：`theme=bloom`（晨樱）、`appearance=light`。首次启动即晨樱浅色；夜间可切 `noir`（夜樱）。

#### 9.2.1 主题预设

| id | 名称 | 明暗 | 主色 | 气质 / 场景 |
|---|---|---|---|---|
| `bloom` | 晨樱 Bloom | 浅 | `#C2255C` 玫瑰 | **默认**；白天、演示、长时间浏览 |
| `noir` | 夜樱 Noir | 深 | `#F06595` 樱粉 | 夜间、低光环境、OLED |
| `lilac` | 薰衣草 Lilac | 浅 | `#6D4FC4` 紫罗兰 | 冷静柔和，偏理性 |
| `mocha` | 奶咖 Mocha | 浅 | `#A9674E` 焦糖 | 暖中性，长时间录入 |
| `mint` | 薄荷 Mint | 浅 | `#2F8F7E` 青绿 | 清爽，中性偏冷 |

> 预设只替换 token 值（`css/themes/<id>.css`），组件 CSS 不感知主题；新增主题 = 新增一个 30 行的变量文件。

#### 9.2.2 晨樱 Bloom（默认，浅色）

| Token | 值 | 用途 |
|---|---|---|
| `--bg-0` | `#FFF7F9` | 窗口底（樱粉白） |
| `--bg-1` | `#FFFFFF` | 面板/卡片/编辑区 |
| `--bg-2` | `#FCEFF3` | 表头/输入框/悬浮层 |
| `--bg-3` | `#F8E1E8` | 选中/hover/按下 |
| `--bg-stripe` | `#FEFAFB` | 网格斑马纹（与 bg-1 明度差 < 2%） |
| `--border` | `#F1DEE5` | 分割线 |
| `--border-strong` | `#E4C6D1` | 输入/按钮描边 |
| `--text` | `#3B2A33` | 主文本（梅子墨，白底约 12:1） |
| `--text-dim` | `#7F6775` | 次要文本（约 5.1:1） |
| `--text-faint` | `#A993A0` | 占位/禁用（仅非关键信息） |
| `--accent` | `#C2255C` | 主色：主按钮/焦点/活动标签/当前连接 |
| `--accent-hover` | `#A81D4E` | 主色 hover |
| `--accent-soft` | `#FCE7EF` | 主色淡底（选中行/徽标底） |
| `--on-accent` | `#FFFFFF` | 主色上的文字 |
| `--accent-2` | `#7C5CD6` | 辅助色：链接/图表第 2 色 |
| `--accent-2-soft` | `#F2EDFC` | 辅助色淡底 |
| `--success` | `#197A50` | 成功/已连接/只读 |
| `--warning` | `#A15C00` | 警告/SQLite |
| `--danger` | `#C92A2A` | 错误/删除（**独立暖红，不用粉色**） |
| `--info` | `#1C64B0` | 提示/计划 |
| `--focus-ring` | `rgba(194,37,92,.30)` 3px | 键盘焦点 |
| `--selection` | `#F9D3E0` | 文本选择 |
| `--shadow-1` | `0 1px 2px rgba(92,35,55,.06)` | 卡片 |
| `--shadow-2` | `0 10px 30px rgba(92,35,55,.12)` | 弹层/对话框 |
| `--mono` | `"Cascadia Code", "Consolas", monospace` | 编辑器/数据 |

**SQL 语法色（bloom）**：关键字 `#A61E5A`（600）、字符串 `#1F6F5C`、数字 `#6D4FC4`、注释 `#7F6775`（斜体）、函数 `#96543C`、类型/内置 `#1C64B0`、运算符 `#5C4652`、`NULL` `#A15C00`、当前行 `#FFF0F5`。全部在 `#FFFFFF` 上 ≥ 4.5:1。

#### 9.2.3 夜樱 Noir（深色）

| Token | 值 | 用途 |
|---|---|---|
| `--bg-0` | `#17121A` | 窗口底（深梅黑） |
| `--bg-1` | `#1E1722` | 面板 |
| `--bg-2` | `#271E2C` | 表头/输入 |
| `--bg-3` | `#332637` | 选中/hover |
| `--bg-stripe` | `#1B141F` | 斑马纹 |
| `--border` | `#3A2C40` | 分割线 |
| `--border-strong` | `#4B3A52` | 输入描边 |
| `--text` | `#F4E9EF` | 主文本 |
| `--text-dim` | `#B7A2AE` | 次要文本（约 7:1） |
| `--text-faint` | `#8A7682` | 占位/禁用 |
| `--accent` | `#F06595` | 樱粉：链接/图标/焦点 |
| `--accent-hover` | `#FF85AC` | hover |
| `--accent-soft` | `#3A2230` | 淡底 |
| `--on-accent` | `#2B0A18` | 主色上的文字（粉底深字） |
| `--accent-2` | `#A78BFA` | 辅助紫 |
| `--accent-2-soft` | `#2E2440` | 辅助紫淡底 |
| `--success` | `#4AD295` | 成功 |
| `--warning` | `#F0B24A` | 警告 |
| `--danger` | `#FF7A85` | 错误/删除 |
| `--info` | `#7FB3F0` | 提示 |
| `--focus-ring` | `rgba(240,101,149,.35)` 3px | 焦点 |
| `--selection` | `#4A2334` | 文本选择 |
| `--shadow-1` | `0 1px 2px rgba(0,0,0,.35)` | 卡片 |
| `--shadow-2` | `0 14px 40px rgba(0,0,0,.5)` | 弹层 |

**SQL 语法色（noir）**：关键字 `#FF8FB1`、字符串 `#7FD6B4`、数字 `#C4A9FF`、注释 `#9C8794`、函数 `#E8A87C`、类型 `#8CC2F5`、`NULL` `#F0B24A`。深色底上均 ≥ 4.5:1。

#### 9.2.4 用色规则

1. **粉色是有面积预算的**：主色只出现在主按钮、焦点环、活动标签下划线、当前连接圆点、选中行左侧 2px 指示条；占屏面积 ≤ 8%，大面积保持中性。
2. **语义与品牌分离**：删除/错误永远用 `--danger`（暖红），不与玫瑰主色混用；成功不用粉色；只读连接用 `--success` 小圆点 + 文本，不整行染色。
3. **数据优先**：网格斑马纹明度差 ≤ 2%，选中行用 `--accent-soft` 而非高饱和；JSON/BLOB 预览、执行计划等只读区不做粉色装饰。
4. **对比度门槛**：正文 ≥ 7:1，次要文本 ≥ 4.5:1，图标/边框 ≥ 3:1；M8 增加 `tools/contrast_check.py` 校验全部主题的关键配对，未达标 CI 失败。
5. **形状与层次**：控件圆角 10px、输入 8px、弹层 14px、标签 8px；用两层柔和阴影 + 1px 低对比描边表达层级，不用粗边框和纯黑阴影。
6. **动效克制**：120–160ms `cubic-bezier(.2,.8,.2,1)`；只做透明度/位移；`prefers-reduced-motion` 或 `ui.reduceMotion=true` 时全部关闭。
7. **字体**：界面 `"Microsoft YaHei UI", "Segoe UI"`，数据/编辑器 `--mono`；所有数字列 `font-variant-numeric: tabular-nums`。
8. **图标**：1.5px 线性 SVG（16/18px），激活态 `--accent`，其余 `--text-dim`；不使用图标字体、不使用 emoji 作功能图标。
9. **装饰渐变**：仅欢迎页/空状态可用 `linear-gradient(135deg,#FFF0F5,#F4EFFC)` 这类极浅玫瑰→薰衣草渐变；工作区禁用渐变背景。

#### 9.2.5 主题加载与切换

- `<html data-theme="bloom" data-appearance="light">`，主题文件只含变量；组件 CSS 只引用语义 token。
- `appearance` 支持 `light | dark | system`；选 `system` 时监听 `matchMedia('(prefers-color-scheme: dark)')`。
- **防白闪**：`index.html` 头部内联脚本，先从 `localStorage` 读取主题并设置 `data-*` 再加载 CSS；C++ 侧窗口创建时即用对应 `--bg-0`（`Silkit` 默认 `#FFF7F9`），`theme.set` 再同步 `put_DefaultBackgroundColor`。
- CodeMirror 主题随 `data-theme` 切换 `setOption('theme', ...)`，语法色集中在 `css/themes/cm-<id>.css`。
- 主题预览：设置页 5 张色卡（主色/底色/文字三点），点击即时预览，确认后写入 `settings.json`。
- **顶栏切换器**：右侧 `主题·晨樱 ▾` 按钮（当前主题名 + 3 色点），点击弹出面板：5 套主题 × 明暗标签，键盘 `↑/↓` 选择、`Enter` 应用、`Esc` 关闭；`Ctrl+Shift+T` 与按钮等效。
- **明暗快捷键**：`Ctrl+Shift+L` 在「当前浅色主题」与 `noir` 之间切换，并记住上次的浅色主题；手动切换会把 `followSystem` 置为 `false`，避免和系统主题打架。

### 9.3 前端模块

| 文件 | 职责 | 关键 API |
|---|---|---|
| `bridge.js` | 原生桥/浏览器 mock 回退 | `invoke`, `listen`, `call`, `cancel`, `isNative` |
| `api.js` | 命令封装、reqId pending、事件路由 | `api.conn.test()`, `api.query.execute()` |
| `state.js` | 全局状态、订阅、派生 | `state.get()`, `state.patch()`, `state.subscribe()` |
| `app.js` | 启动、快捷键、全局错误 | `boot()` |
| `themes.js` | 主题应用、系统明暗监听、防闪、CodeMirror 同步 | `applyTheme(theme, appearance)` |
| `layout.js` | 侧栏/分栏/标签/状态栏 | `layout.render()` |
| `views/tree.js` | 对象树懒加载、过滤、右键菜单 | `tree.load(node)` |
| `views/query.js` | SQL 标签页生命周期 | `queryTab.create()` |
| `views/query_context.js` | 查询页连接/库下拉、自动切库、补全缓存刷新、无标签空态 | `SilkitQueryContext.create(app).init()` |
| `views/results.js` | 多结果面板/消息/计划 | `results.show()` |
| `views/table.js` | 数据标签页/编辑状态 | `tableTab.open()` |
| `views/design.js` | 表设计器 | `designTab.open()` |
| `components/grid.js` | 虚拟滚动、选择、复制、列宽 | `new Grid(host, opts)` |
| `components/editor.js` | CodeMirror 封装/补全/执行 | `new SqlEditor(host, opts)` |
| `editor/hint.js` | 关键字 + 表/视图 + 列智能补全（按连接/库隔离） | `SilkitHint.create(app)` |
| `components/cellviewer.js` | 长文本/JSON/图片/HEX | `openCellViewer(cell)` |

**状态形状（节选）**

```js
const state = {
  meta: { version: '1.0.4', engines: ['mysql', 'postgres', 'sqlite'] },
  connections: [],                    // 无密码
  sessions: {                         // sessionId -> runtime
    s1: { connectionId: 'c1', status: 'connected', serverVersion: '8.4.3',
          currentDb: 'app', tx: 'autocommit', capabilities: {} }
  },
  tabs: [], activeTab: null,
  tree: { nodes: {}, expanded: new Set(), filter: '' },
  settings: {},
  jobs: {}                            // 导入导出进度
};
```

### 9.4 连接管理

**连接列表（侧栏顶部 / 欢迎页）**
- 按 `group` 分组，可折叠；连接显示颜色点、引擎图标、只读锁。
- 右键：连接/断开、编辑、复制、删除、新建查询、在资源管理器中打开 SQLite 文件。
- 最近使用置顶；支持搜索。

**连接对话框**
- 引擎选择：三个卡片（MySQL/MariaDB、PostgreSQL、SQLite）。
- 公共字段：名称、分组、颜色、只读。
- MySQL：主机、端口、用户、密码、数据库、字符集、SSL 模式、连接超时、初始 SQL。
- PG：主机、端口、用户、密码、数据库、schema、SSL 模式、application_name。
- SQLite：文件路径（浏览）、打开方式（读写/只读）、busy timeout、外键开关、WAL 提示。
- “测试连接”异步执行，按钮显示 loading；成功后显示服务器版本与耗时。
- “保存密码”复选框；不勾选时连接弹窗输入。
- 高级折叠区：超时、keepalive、init SQL。

### 9.5 对象树

**层级**

| 引擎 | 层级 |
|---|---|
| MySQL | 连接 → 数据库 → [表 / 视图 / 函数 / 触发器 / 事件] → 表 → 列/索引/外键 |
| PostgreSQL | 连接 → 数据库 → schema → [表 / 视图 / 物化视图 / 函数 / 序列 / 触发器] → 表 → 列/索引/约束 |
| SQLite | 连接（文件）→ main/attached → [表 / 视图 / 索引 / 触发器] → 表 → 列/索引/外键 |

**行为**
- 展开时才请求 `meta.children`；节点缓存 60s；刷新按钮强制 `refresh:true`。
- 过滤框：对已加载节点做即时过滤；输入 ≥2 字符且节点未加载时，触发 `meta.search` 服务端搜索（最多 200 条）。
- 节点状态：加载中（旋转）、空（“无对象”）、错误（红色 + 重试）。
- 双击表 = 打开数据标签；双击视图 = 打开数据（只读）；双击函数/触发器 = 查看 DDL。
- 右键菜单按 `kind` 动态生成；所有破坏性项标红并需输入对象名确认。
- 拖拽：`.sql` 文件到窗口打开查询；`.db/.sqlite/.sqlite3` 打开连接对话框；`.csv` 打开导入向导（v1.1）。

### 9.6 标签页

| 类型 | 内容 | 关闭确认 |
|---|---|---|
| `query` | SQL 编辑器 + 结果 | SQL 或结果有变更时 |
| `table` | 数据网格 + 筛选/分页 | 有未提交编辑时 |
| `design` | 表设计器 | 有未应用修改时 |
| `ddl` | 只读 DDL | 否 |
| `welcome` | 欢迎页（唯一） | 否 |
| `settings` | 设置（唯一） | 否 |

- 标签标题：查询显示“查询 N”或文件 basename；表显示表名；设计显示“设计: 表名”。
- 未保存标记 `●`；连接断开时标题变灰 + 断线图标。
- 快捷键：`Ctrl+T` 新查询、`Ctrl+W` 关闭、`Ctrl+Tab` 下一个、`Ctrl+1..9` 切换、中键关闭。
- 关闭全部时若有未保存内容，统一确认。
- **无标签空态（v1.0.2）**：启动、连接成功、关闭最后一个标签都不自动补建查询页；主区域显示 `#workspaceEmpty`（新建查询 / 打开 SQL / `Ctrl+N` 引导），编辑器、结果、表格与分割条全部隐藏。空标签集随 `settings.query.tabs` 持久化，重启后仍保持关闭；用户显式新建、打开 `.sql` 或拖放时才创建标签。

### 9.7 SQL 编辑器

- CodeMirror 5：SQL 模式、行号、括号匹配、自动闭合、当前行高亮、搜索替换、Tab 缩进。
- 自动补全（`Ctrl+Space` 或输入字母/点号 220ms 后触发；v1.0.2 重写、v1.0.3 / v1.0.4 增强 `frontend/js/editor/hint.js`）：
  - SQL 关键字 / 函数（内置列表，按引擎过滤）；
  - 当前库的表与视图，按 `会话｜引擎｜库｜schema` 异步按需拉取并缓存，切库 / DDL 变更自动失效；
  - 当前语句 `FROM/JOIN/UPDATE/INTO` 中出现的表的列，作为裸标识符候选；
  - 输入 `表名.` 或 `别名.` 时只提示该表的列，候选带类型说明，替换区间从点号之后开始；
  - PG 透传 schema 限定名；无连接时不拉起 anyword 兜底；
  - **严格隔离（v1.0.2 回归修复）**：点号后只返回已确认存在的表的列，解析不到就返回空，绝不退化成关键字、表名或其它表的列；`FROM / JOIN / UPDATE / INTO` 后的 `库名.`（PG `schema.`）视为库限定符，改提示该库的表名；裸标识符分支也只补当前库中真实存在的表的列，避免把 `db.column` 里的列名当表名串列；
  - **候选排序**：`COMMON_KEYWORDS` 划分常用项，顺序为常用关键字（0）→ 表名（1）→ 列名（2）→ 冷门关键字（3，函数 / DDL 子句 / 引擎专属项保留但排末尾）。
  - **整条语句解析（v1.0.3）**：`statementAt()` 以分号切分光标所在语句（含光标之后的 `FROM`），`tableRefs()` 剥离 `--` / `/* */` 注释后解析表引用；列候选只取该语句中真实存在的表，语句已写出表时不再混入对象树选中的表。因此 `SELECT departments.dept_no, d| FROM departments` 输入 `d` 也能提示 `dept_name`；
  - **右侧所属路径（v1.0.3，v1.0.4 收窄）**：`show-hint` 的 `hint.render` 把名称放左、类型与来源右对齐——列 `库.表`（PG `schema.表`，跨库限定符随目标库变化）、表库名；去掉名称后的 `(table)` / `(view)`，CSS 复用 `--text-dim` / `--text-faint` / `--accent-hover`。v1.0.4 起关键字不带 `_source` / `_badge`，不生成 `.cm-hint-meta`，右侧留空。
- 执行：
  - `F5`：执行全部语句（按分割器顺序）；
  - `Ctrl+Enter`：执行光标所在语句；
  - `Ctrl+Shift+Enter`：执行选中文本；
  - 工具栏按钮：运行、运行选中、取消、解释、格式化。
- 多语句结果：每个 SELECT 一个“结果 N”子标签；DML 进“消息”；错误进“消息”并高亮行。
- 错误定位：后端返回 `statementIndex` + `position`；前端用分割偏移换算到行列，CodeMirror `markText` 红色波浪线。
- 格式化：`sql_format.js` 轻量实现（关键字大写、主子句换行、逗号对齐）；不追求完美，失败时保留原文并提示。
- 保存/打开 `.sql`：UTF-8 优先；检测 BOM/UTF-16/GBK；保存默认 UTF-8。
- 历史：每次执行记录连接、数据库、SQL、耗时、行数、状态；点击插入编辑器；支持搜索/清空。
- 片段：命名、分组、插入；内置常用片段（SELECT/INSERT/UPDATE/DELETE/建表模板）。


#### 9.7.1 查询上下文与补全元数据（v1.0.2 起，v1.0.3 / v1.0.4 增强）

- `frontend/js/views/tree.js` 的 `selectNode()` 暴露当前选中连接 / 库 / schema / 表；顶栏「新建查询」、标签栏 `+`、`Ctrl+N` 通过 `tabs.openQueryFromTree()` 把该上下文传给 `openQuery()`。
- `frontend/js/views/query_context.js` 提供编辑器工具条的「连接」「库」下拉：改连接 / 改库立即调用 `conn.set_database`（MySQL 内部 `USE`、PG 重连、SQLite 只允许已 ATTACH 的库），清空该会话补全缓存并刷新对象树；库列表按当前连接异步加载。
- 每个查询标签持久化 `connectionId / database / schema / table`，随会话恢复写回 `settings.json`；激活标签或执行 SQL 前（`query.js`）若会话当前库与本标签不一致则自动切回，避免多标签共用一个会话时互相污染；切库失败会回退标签上的库并提示，不阻断本次执行。
- 补全元数据以 `会话｜引擎｜库｜schema` 为键缓存，首次触发按需拉取、不阻塞输入；`Ctrl+Space`、自动触发、切换连接 / 库、编辑器获得焦点均走同一入口刷新候选。
- **空态约束**：无标签时两个下拉随编辑器一起隐藏（`syncEmptyState()`），`queryContext.sync()` 仍可安全执行（上下文退化为无会话），不会因空标签触发切库或新建页。
- **补全来源标注（v1.0.3，v1.0.4 收窄）**：候选在 `makeItems()` 里携带 `_badge` / `_source`，`renderHintItem()` 生成 `.cm-hint-label` 与 `.cm-hint-meta`（`.cm-hint-badge` + `.cm-hint-source`）；`displayText` 保留纯文本降级。`scopeLabel()` 决定 MySQL 用库名、PG 用 schema 名。v1.0.4 起关键字两档均不传 `source`，只显示名称。

### 9.8 结果面板

- 子标签：`结果 1..n`、`消息`、`执行计划`。
- 网格能力：
  - 虚拟滚动（只渲染可视行 + 缓冲 10 行）；
  - 列宽拖拽、双击自适应；
  - 列显示/隐藏菜单；
  - 点击表头排序（已加载行客户端排序；表数据走服务端）；
  - 单元格/区域选择（Shift+点击/拖拽）、`Ctrl+A`、`Ctrl+C`；
  - 复制格式：TSV（默认，Excel 友好）、CSV、JSON、INSERT、Markdown；
  - 状态栏：行数、耗时、受影响行数、警告数；
  - “继续加载/导出全部”按钮（当 `truncated`）。
- 单元格查看器：
  - 长文本：等宽滚动、自动换行开关、搜索；
  - JSON：格式化/折叠（轻量实现，避免大对象卡顿）；
  - BLOB：HEX 视图、前 64KB 预览、图片自动识别（png/jpg/gif/webp/bmp）、另存为；
  - NULL：灰色斜体 `NULL`，编辑时用 `Ctrl+0` 切换。
- 消息：每个语句一行（成功/失败/耗时/affected/info）；错误展开详情（SQLSTATE、position、hint）。
- 执行计划：v1 文本树（`EXPLAIN` 结果直接渲染为网格）；PG JSON 计划 v1.1 可视化。

### 9.9 数据网格（表数据）

**工具栏**：刷新、添加行、删除选中行、保存、撤销更改、筛选、排序、每页条数、上一页/下一页、精确计数、导出。

**编辑模型**

```js
tab.edit = {
  rows: [],                    // 原始行
  columns: [],                 // 含 pk/identity/editable
  changes: {
    inserts: new Map(),        // tempId -> {values}
    updates: new Map(),        // rowKey -> {changes}
    deletes: new Set()         // rowKey
  },
  dirty: false
};
```

- 双击/`F2`/`Enter` 进入编辑；`Esc` 取消当前单元格；`Tab` 下一列；`Ctrl+Enter` 保存全部。
- 类型感知输入：整数/小数校验；日期时间用文本 + 示例；布尔用下拉（true/false/1/0）；NULL 切换；BLOB 打开查看器。
- 视觉：新增行淡绿、删除行删除线、修改单元格左上角橙色三角。
- 行标识：优先主键；无主键但有唯一非空索引可用；否则只读并提示原因。
- 保存：`table.apply`（dryRun 先取 SQL 预览）→ 事务执行 → 成功刷新当前页；失败回滚全部并定位错误行。
- 删除：先标记，保存时统一执行；关闭标签前确认。
- 筛选：列 + 操作符（`= != > >= < <= LIKE NOT LIKE IN IS NULL IS NOT NULL BETWEEN`）+ 值；AND 组合（v1），OR 分组 v1.1。
- 排序：点击表头；多列排序 v1.1。
- 分页：`LIMIT pageSize+1 OFFSET page*pageSize`；`hasMore` 多取 1 行判断；总数为估算，精确计数按需。
- 大表：offset 很大时提示“深分页较慢”；v1.1 改 keyset。
- 复制/粘贴：单元格/区域复制为 TSV；从 Excel 粘贴到网格 v1.1。
- 只读连接：工具栏禁用写操作，后端二次拦截。

### 9.10 表设计器

**Tab**：列 / 索引 / 外键 / 选项 / SQL 预览。

- 列：名称、类型（引擎类型下拉 + 长度/精度）、非空、默认值、自增/identity、注释、字符集/排序规则、生成表达式。
- 索引：名称、唯一、类型（BTREE/HASH；PG 另 GIN/GIST/BRIN）、列顺序、前缀长度（MySQL）、条件（PG partial）。
- 外键：名称、本表列、引用 schema/表/列、ON UPDATE/DELETE。
- 选项：MySQL 引擎/字符集/排序规则/注释；PG 表空间/所有者（v1.1）；SQLite WITHOUT ROWID/STRICT（按版本）。
- SQL 预览：实时生成；语法高亮；可复制；不可直接编辑执行（避免绕过校验）。
- 应用：
  1. `ddl.preview` 生成语句 + 警告；
  2. 确认对话框显示语句、影响（丢列/改类型）；
  3. `ddl.apply` 执行；
  4. 成功刷新树与标签；失败显示错误并保留修改。
- 已知限制提示：
  - MySQL：DDL 自动提交，无法回滚；
  - PG：支持事务 DDL（可回滚）；
  - SQLite：复杂变更需重建表，v1 仅支持有限 ALTER。
- 新建表：空设计器；选择数据库/schema；模板（id 主键 + created_at）。
- 删除/重命名/清空：右键菜单，输入对象名确认；清空使用 `TRUNCATE`（SQLite 用 `DELETE` + 重置 sequence）。

### 9.11 导入 / 导出

**导出向导**
1. 来源：当前结果集 / 当前表 / 整个数据库（结构+数据）。
2. 格式：CSV、JSON、SQL、Markdown、HTML。
3. **结果集导出模式**：
   - `rerun`（默认）：后端重跑 SQL 并流式写文件，能导出全量、内存占用低；非确定性查询（`NOW()`/`RANDOM()`）结果可能变化。
   - `snapshot`：前端把当前已加载行分块回传，导出所见即所得；受 `maxRows` 限制。
4. 选项：
   - CSV：分隔符、引号、表头、NULL 字面量、换行、编码（UTF-8/UTF-16LE/GBK）、BOM；
   - JSON：对象数组 / 数组数组 / 每行一个 JSON（JSONL）；
   - SQL：INSERT 批量大小、是否包含结构、是否 DROP IF EXISTS；
   - Markdown/HTML：表头、对齐。
4. 路径 + 进度 + 取消；完成后“打开所在文件夹”。

**CSV 导入向导**
1. 选文件 → 探测编码/分隔符/引号/是否有表头 → 预览前 50 行。
2. 目标：现有表（映射列）或新建表（推断类型）。
3. 映射：源列 → 目标列；忽略列；NULL 标记（空字符串/`NULL`/自定义）。
4. 选项：批量大小（默认 500）、遇错停止/跳过、是否事务、是否先清空表。
5. 执行：`io.progress` 显示已处理/总数/速度；失败给出 CSV 行号。
6. 完成：显示成功/跳过/失败统计；失败默认全部回滚。

**SQL 文件执行**
- 打开 `.sql` → 分割器 → 顺序执行；显示当前语句/总数；遇错停止（默认）或继续；可选事务包裹（PG/SQLite）。
- 大文件流式读取，不一次性载入内存（超过 16MB 时按语句块读取）。

**转储**
- 范围：整库 / 指定 schema / 指定表；结构 / 数据 / 两者。
- 选项：DROP IF EXISTS、事务包裹、批量 INSERT、包含触发器/函数、包含注释。
- 输出：`.sql` 文件；进度可取消；失败保留部分文件并提示。

### 9.12 设置页 / 欢迎页

**设置**：通用（语言/启动/确认）、外观（5 套主题色卡、明暗/跟随系统、减弱动效）、编辑器、查询、数据网格、导出默认值、快捷键（只读展示）、关于（版本/许可/日志目录/第三方声明）。

**欢迎页**：最近连接（最多 8 个）、新建连接、打开 SQLite 文件、最近 SQL（5 条）、快捷键卡片、版本与链接。

### 9.13 快捷键

| 快捷键 | 功能 |
|---|---|
| `Ctrl+N` | 新建连接 |
| `Ctrl+T` | 新建查询 |
| `Ctrl+W` | 关闭当前标签 |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | 下一个/上一个标签 |
| `Ctrl+1..9` | 切换到第 N 个标签 |
| `F5` | 执行全部 |
| `Ctrl+Enter` | 执行当前语句 |
| `Ctrl+Shift+Enter` | 执行选中 |
| `Esc` | 取消执行 / 关闭弹层 |
| `Ctrl+S` | 保存 SQL 文件 / 应用表修改 |
| `Ctrl+Shift+S` | 另存为 |
| `Ctrl+B` | 折叠/展开侧栏 |
| `Ctrl+Shift+T` | 打开/关闭主题面板 |
| `Ctrl+Shift+L` | 浅色 ↔ 深色（夜樱）切换 |
| `1`–`5`（主题面板聚焦时） | 快速选择第 N 套主题 |
| `Ctrl+F` | 编辑器/网格搜索 |
| `Ctrl+Space` | 自动补全 |
| `F2` | 重命名对象 / 编辑单元格 |
| `Delete` | 删除选中行（标记） |
| `Ctrl+0` | 单元格设为 NULL |
| `F12` | DevTools（开发/调试） |

### 9.14 交互细则

- **加载**：>150ms 显示骨架/旋转；>2s 显示“仍在执行…取消”。
- **空状态**：树节点、结果集、历史、搜索均有说明文字与下一步按钮。
- **错误**：Toast（3s）+ 详情弹窗（可复制）；错误文案包含“做什么失败 + 原因 + 建议”。
- **确认**：破坏性操作使用模态框，显示对象全名、语句预览、影响提示；危险项要求输入对象名。
- **焦点**：弹窗打开后焦点进入首个输入框；`Esc` 关闭；`Tab` 循环。
- **IME**：编辑器/输入框不拦截中文输入法组合键；快捷键在 `compositionstart` 期间禁用。
- **复制**：网格复制保留 NULL 为空、二进制为 `0x...`；复制 SQL 时按引擎引用。
- **性能**：列表 >200 项分页/虚拟滚动；网格只绑定可视行；避免每次输入全量重渲染。
- **可访问性**：可见焦点环、`aria-label`、键盘可操作树/网格/弹窗；对比度 ≥ AA。

### 9.15 国际化

- `i18n.js`：`zh-CN`（默认）、`en-US`；`t('query.run')`。
- HTML 静态文案用 `data-i18n`；动态文案用 `t()`；数字/日期用 `Intl`。
- 引擎错误原文保留，附中文说明前缀（如“执行失败：”）。
- 语言切换即时生效（重渲染当前视图），持久化到 settings。

---

## 10. 功能规格与数据库行为

### 10.1 对象浏览规格

| 对象 | MySQL | PostgreSQL | SQLite |
|---|---|---|---|
| 数据库列表 | `information_schema.SCHEMATA` | `pg_database` | `PRAGMA database_list` |
| 表列表 | `TABLES` | `pg_class` | `sqlite_master` |
| 列 | `COLUMNS` | `pg_attribute` + `format_type` | `PRAGMA table_xinfo` |
| 索引 | `STATISTICS` | `pg_index` + `pg_get_indexdef` | `PRAGMA index_list/info` |
| 外键 | `KEY_COLUMN_USAGE` + `REFERENTIAL_CONSTRAINTS` | `pg_constraint` | `PRAGMA foreign_key_list` |
| 函数/过程 | `ROUTINES` | `pg_proc` | — |
| 触发器 | `TRIGGERS` | `pg_trigger` | `sqlite_master` |
| 事件 | `EVENTS` | — | — |
| 序列 | — | `information_schema.sequences` | — |
| DDL | `SHOW CREATE ...` | 目录重建 + `pg_get_*def` | `sqlite_master.sql` |

**刷新策略**
- 连接后加载数据库列表；展开数据库加载表/视图等文件夹。
- 表节点展开加载列/索引/外键（按需）。
- DDL 成功、导入完成、连接重连后失效相关缓存。
- “刷新”按钮：清除该节点及子节点缓存并重新请求。

### 10.2 查询执行规格

1. 前端把编辑器内容 + 选区/光标交给 `sql_splitter`（后端权威实现；前端只传原文与模式）。
2. 后端拆分语句；按顺序执行；每条语句：
   - 记录起始时间；
   - 发送列元数据（若有）；
   - 流式发送行批次；
   - 结束发送语句摘要。
3. 默认遇错停止；`continueOnError=true` 时继续并汇总错误（v1.1 UI 开关）。
4. 所有语句共用一个 `reqId`；事件带 `statementIndex`。
5. 只读会话：执行前做语句分类（`SELECT/SHOW/EXPLAIN/PRAGMA` 允许；其余拒绝并提示）。
6. 安全模式：检测 `DROP/TRUNCATE/ALTER/DELETE 无 WHERE/UPDATE 无 WHERE/GRANT/REVOKE`，执行前弹确认（可在设置关闭）。
7. 超时：默认不限制；设置可配 `timeoutMs`；超时走取消流程。
8. 结果集上限：`maxRows` 默认 5000；达到后 `truncated`，前端可“继续加载”（重新执行并使用更大的 maxRows？）——v1 采用“导出全部”或“重跑并提高上限”，不做游标续读。

### 10.3 表格数据编辑规则

| 规则 | 说明 |
|---|---|
| 可编辑判定 | 有主键，或有唯一非空索引；否则只读（显示原因） |
| 行定位 | 主键值（复合主键按列顺序）；SQLite 可退化 rowid；PG 不使用 ctid（默认） |
| 空值 | `NULL` 与空字符串严格区分；编辑器提供 NULL 切换 |
| 类型转换 | 前端做格式校验；最终由数据库转换，错误回显到单元格 |
| 默认值 | 新行未填的列不出现在 INSERT 中，让数据库取默认值 |
| 自增列 | 新行留空；插入后回填 `last_insert_id`（MySQL）或 `RETURNING`（PG/SQLite 3.35+） |
| 并发 | v1 不做乐观锁；保存时以主键覆盖；v1.1 可选“旧值匹配” |
| 事务 | 一次保存内的所有变更在同一事务；失败全部回滚（默认） |
| 大字段 | 列表默认截断；编辑/查看时 `table.cell` 拉全量 |
| 二进制 | 查看 HEX/图片；编辑提供“上传文件/十六进制文本”两种方式 |

**生成的 SQL 示例（表 `users`，主键 `id`）**

```sql
-- 更新
UPDATE `users` SET `name` = ? WHERE `id` = ?;
-- 删除
DELETE FROM `users` WHERE `id` = ?;
-- 插入
INSERT INTO `users` (`name`, `age`) VALUES (?, ?);
```

- 标识符按引擎引用；值全部参数化；不使用字符串拼接值。
- `table.apply` 返回 `sqlPreview` 与实际执行一致（同一生成器）。
- 批量更新按相同 SET 列分组，准备一次语句循环绑定，提高性能。

### 10.4 表设计器规则

| 引擎 | 支持 | 限制/提示 |
|---|---|---|
| MySQL | CREATE/ALTER/DROP/RENAME/TRUNCATE；列增删改；索引；外键；引擎/字符集/注释 | DDL 自动提交，无法回滚；改类型可能丢数据 |
| PostgreSQL | 同上；事务内执行；列类型/默认值/非空；约束/索引；注释 | 不能调整列顺序；复杂继承/分区/RULE 不支持 |
| SQLite | CREATE/DROP/RENAME TABLE；ADD/RENAME/DROP COLUMN；索引；触发器 | 不能改列类型/约束；复杂变更提示重建表 |

**SQL 预览必须包含**
- 完整语句（含 schema 限定）。
- 警告：丢列、缩短长度、改类型、加唯一约束可能失败、MySQL 不可回滚。
- 影响行数（如 `ALTER` 无法预估则显示“未知”）。

### 10.5 导入 / 导出 / 转储规格

| 功能 | 规格 |
|---|---|
| CSV 导出 | RFC4180；可配置分隔符/引号/表头/NULL/换行/编码/BOM；流式写入 |
| JSON 导出 | 数组对象/数组数组/JSONL；UTF-8；大整数保持字符串 |
| SQL 导出 | 批量 INSERT（默认 500 行/条）；可选结构；按引擎引用 |
| Markdown/HTML | 表格；NULL 显示可配置 |
| CSV 导入 | 编码探测（BOM/UTF-8/GBK）；分隔符探测；前 50 行预览；列映射；批量插入；事务；错误行号 |
| SQL 文件执行 | 分割器；进度；遇错停止/继续；大文件流式 |
| 转储 | 整库/表；结构/数据；DROP IF EXISTS；事务；进度/取消 |
| 取消 | 作业级 `jobId`；写文件先写 `.part`，取消/失败删除 |

**编码**：内部统一 UTF-8；GBK 仅在文件边界转换（`MultiByteToWideChar`/`WideCharToMultiByte`，`CP_GB18030` 优先）。

### 10.6 只读与安全模式

- **只读连接**：连接级 `readOnly`；后端拒绝写语句（即使前端被绕过）；UI 灰掉写按钮。
- **安全模式**（默认开）：危险语句执行前确认；确认框显示语句与目标对象；可选择“本次会话不再提示”（不推荐）。
- **SQL 注入防护**：应用生成的 DML/筛选/导入全部参数化；标识符来自元数据并引用；绝不拼接用户输入的标识符（表设计器的新表名做严格校验：长度、字符集、禁止控制字符/引号）。
- **敏感信息**：日志/历史/崩溃报告过滤密码、连接串中的 `password=`、token；导出连接默认不含密码。
- **传输**：SSL 默认 `preferred`/`prefer`；`verify-ca`/`verify-full` 需用户显式选择并填 CA。

---

## 11. 里程碑与任务分解

> 依赖关系：M0 → M1 → M2 → M3 → M4 → M5 → M6 → M7 → M8 → M9。
> M1（SQLite）先打通全链路，M2/M3 只补驱动与方言，UI 复用。

### M0 工程骨架、异步 IPC、UI 外壳与主题（4–5 d）→ v0.0.1

**目标**：能构建、能启动、能完成一次异步 IPC 往返，UI 骨架可见，五套主题与切换器可用。

- [x] M0-1 复制 `StepDraw\TauriCPP` 到本项目的 `TauriCPP\`（排除 `build/.git`），记录来源 commit。
- [x] M0-2 `CMakeLists.txt`：C++20（M0 交付时为 C++17，2026-11-15 升级）、`/utf-8`、`/W4`、`/permissive-`、静态 CRT、TauriCPP、前端打包、版本/图标/manifest。
- [x] M0-3 `vcpkg.json` + `build.ps1`（toolchain、triplet、`-DevMode`）。
- [x] M0-4 `src/main.cpp`：窗口 1120×700、窗口底色 `#FFF7F9`、DevTools、标题、单实例。
- [x] M0-5 `util/logger`、`store/paths`、`store/settings`、`store/profiles` 骨架。
- [x] M0-6 `ipc/async_router`：线程池、reqId、`ipc.call`/`ipc.cancel`、`ipc.event`、关闭流程。
- [x] M0-7 `frontend` 外壳：顶栏/侧栏/标签栏/编辑区/结果区/状态栏（静态即可）；bloom token 落位。
- [x] M0-8 `bridge.js`/`api.js`：原生桥 + 浏览器 mock 回退；`app.info`、`app.ping`（带进度事件）。
- [x] M0-9 构建脚本输出 `build\Release\Silkit.exe`；首次启动无白屏、F12 可用、关闭不卡死。
- [x] M0-10 `ctest` 占位 + README 骨架 + `.clang-format`。
- [x] M0-11 主题系统：`css/tokens.css` 语义 token + `css/themes/{bloom,noir,lilac,mocha,mint}.css` 五套调色板；`js/themes.js` 负责应用、`prefers-color-scheme` 监听、首帧防闪；`theme.set` 同步 WebView2 窗口底色。
- [x] M0-12 顶栏主题切换器：`主题·晨樱 ▾` 按钮 + 下拉面板（5 套色卡 × 明暗标签）；快捷键 `Ctrl+Shift+T` 开关面板、`Ctrl+Shift+L` 明暗切换、面板内 `1`–`5` 快选；选择写回 `settings.json`。
- [x] M0-13 `tools/contrast_check.py`：校验 5 套主题「正文/次要文本/图标」关键配对，可在 CI 直接运行。

**验收**
- 干净目录执行 `build.ps1` 成功；exe < 35MB。
- `app.ping` 发起后，UI 依次收到 3 条 progress + 1 条 result；取消可中断。
- 关闭窗口时无未捕获异常、无挂起线程。
- 首屏即晨樱浅色（无白闪）；`tools/contrast_check.py` 可运行并通过基准 token。
- 5 套主题即时切换、重启后保持；切换/最小化还原均无白闪，窗口底色同步。
- `Ctrl+Shift+T` 面板、`Ctrl+Shift+L` 明暗、`1–5` 快选均可用；焦点在编辑器输入时不误触发。

### M1 SQLite 垂直切片（5–6 d）→ v0.1.0

> 状态：功能项已交付（2026-11-13）；M1-9 以偏差记录中的前端持久化方式实现，性能验收与浏览器端搜索渲染仍待补，见 §0.1 / §0.2。

**目标**：从打开 `.db` 文件到执行 SQL、浏览结果，端到端可用。

- [x] M1-1 `db/types.h`、`driver.h`、`driver_registry`。
- [x] M1-2 `SqliteConnection`：open/ping/execute/stream/cancel/close；PRAGMA 初始化。
- [x] M1-3 `Session`/`SessionManager`：线程亲和、任务队列、关闭/重连。
- [x] M1-4 `conn.*` 命令 + SQLite 连接对话框 + 文件选择。
- [x] M1-5 `meta.children`/`meta.detail`/`meta.ddl`（SQLite 版）。
- [x] M1-6 `query.execute` 流式 + `query.cancel` + 错误定位。
- [x] M1-7 对象树（SQLite）+ 标签页 + 只读结果网格。
- [x] M1-8 结果网格虚拟滚动、列宽、复制 TSV、单元格查看器（文本/BLOB）。
- [x] M1-9 历史记录（SQLite 会话）与设置持久化。
- [x] M1-10 单元测试：SQLite 内存库 CRUD/类型/取消；`test_sqlite_driver`。

**验收**
- 打开示例库，树显示表/视图/索引/触发器；双击表出数据。
- `SELECT`/`INSERT`/`UPDATE`/`DELETE` 正确；affected rows 正确。
- 10 万行结果滚动流畅（≥55fps），内存 < 350MB。
- 长递归 CTE 点击取消 < 500ms 返回；连接仍可继续查询。

### M2 MySQL / MariaDB（6–7 d）→ v0.2.0

> 状态：2026-11-14 已交付并对照真实 MySQL 8.4.13 与 MariaDB 客户端库验收（连接/pong/对象树/DDL/取消/错误提示全通过）。

- [x] M2-1 `MySqlConnection`：`libmariadb` 连接、SSL 文件/强制模式、`utf8mb4`、超时、只读拦截。
- [x] M2-2 文本协议 `mysql_use_result` 流式 + prepared statement 参数化（结果集按 `max_cell_bytes` 缓冲）。
- [x] M2-3 取消：控制连接 `KILL QUERY <thread_id>` + 2006/2013 断线提示。
- [x] M2-4 元数据：`information_schema` 的库/表/视图/列/索引/外键/函数/触发器/事件。
- [ ] M2-5 DDL：已实现 `SHOW CREATE ...` 读取；库/表创建、删除、重命名并入 M6。
- [x] M2-6 连接对话框 MySQL 表单 + 测试连接 + 保存密码（DPAPI）。
- [x] M2-7 `USE \`db\`` 切库、字符集/时区/版本展示。
- [x] M2-8 集成测试：`tests/integration_db.cpp` 按 env DSN 触发，已用 MySQL 8.4.13 实跑通过；MariaDB 同驱动未单独部署。
- [x] M2-9 错误映射：1045/1049/1064/1146/1062/1205/1213/2002/2003/2006/2013。
- [ ] M2-10 性能：5000 表元数据加载 < 1.5s（未实测）。

**验收**
- MySQL 8.4.13（本地，root@127.0.0.1 密码 123456）连接、`SELECT 1`、对象树、列类型、`SHOW CREATE TABLE`、取消、错误提示全部通过（应用内 CDP 探针 + `integration_db`）。
- 修复：MySQL `information_schema` 的 `longtext` 列（如 `COLUMN_TYPE`）以 `MYSQL_TYPE_BLOB` 返回，原按 `BINARY_FLAG` 误判为二进制而 base64 编码；改为以字符集 `charsetnr == 63` 判定真二进制。
- utf8mb4 默认；`binary(16)`/`decimal`/`datetime(6)`/`json` 类型映射见 `engine_dialect`。
- MariaDB 11 同驱动未单独部署，路径一致。

### M3 PostgreSQL（6–7 d）→ v0.3.0

> 状态：2026-11-14 已交付并对照真实 PostgreSQL 18.0.7 验收（连接/pong/对象树/DDL/取消/错误提示全通过）。

- [x] M3-1 `PgConnection`：`PQconnectdbParams`、SSL、`connect_timeout`、`search_path`、`bytea_output=hex`。
- [x] M3-2 `PQsendQueryParams` + `PQsetSingleRowMode` 流式；参数一律文本格式绑定。
- [x] M3-3 `PQcancel` 取消 + `PQbackendPID` 展示。
- [x] M3-4 元数据：`pg_catalog` 的库/schema/表/视图/物化视图/列/索引/约束/函数/序列/触发器。
- [ ] M3-5 DDL：视图/物化视图/函数/索引/触发器用 `pg_get_*def`；表由目录**简化重建**（列/类型/默认值/NOT NULL/PK，索引/外键/注释见详情面板），与 `pg_dump --schema-only` 有差距。
- [x] M3-6 连接对话框 PG 表单 + schema 选择；切库走 `SessionManager::Reconnect`（新建连接）。
- [x] M3-7 类型：OID → `ValueKind`，`bytea` 的 `\x...` 转 base64，jsonb/数组/uuid/timestamptz/interval 保留服务端文本。
- [x] M3-8 集成测试：`tests/integration_db.cpp` 按 env DSN 触发，已用 PostgreSQL 18.0.7 实跑通过（含 13–16 同驱动路径）。
- [x] M3-9 错误映射：`PG_DIAG_SQLSTATE`（42/23/28/3D/53/08/40/57P01）、detail/hint/position。
- [ ] M3-10 性能：1000 表 schema 树展开 < 1s（未实测）。

**验收**
- PostgreSQL 18.0.7（本地，postgres@localhost 密码 123456，库 `silkit_test`）连接、`SELECT 1`、对象树、列类型（`integer`/`character varying(64)`/`timestamp with time zone`）、目录重建 DDL（带简化实现警告）、取消、错误提示全部通过（应用内 CDP 探针）。
- 切库走重连；多 schema 浏览正确。
- 类型显示与错误映射见 `engine_dialect`。

### M4 查询工作区（6–7 d）→ v0.4.0

> 状态：2026-11-14 已交付垂直切片并经运行时验收（CDP 探针 + 真实 MySQL/PG）。

- [x] M4-1 CodeMirror 5 接入（vendor 自 npm tarball；每标签独立 Doc；主题映射语义 token）。
- [x] M4-2 标签页管理：新建/关闭/切换 + 重启恢复（settings.query.tabs，最多 20 个）。
- [x] M4-3 执行范围：选中文本 / 光标所在语句 / 全部；多结果集以子标签展示（`#resultSets`）。
- [x] M4-4 自动补全：Ctrl+Space 与输入触发；表/列来自 `meta.children` 缓存（预取 ≤60 表）+ 引擎关键字。
- [x] M4-5 错误行高亮：行背景 + gutter 标记 + markText，定位自 `error.offset`。
- [x] M4-6 取消查询：双通道（`task.cancel` + `query.cancel`）沿用 M1 实现。
- [x] M4-7 历史（5000 条上限）+ 片段（200 条）：搜索、插入光标处、双写 localStorage + settings。
- [x] M4-8 `.sql` 打开/保存/另存：新 IPC `app.read_file`/`app.save_file`（限文本扩展名、16MB）；拖放打开。
- [x] M4-9 轻量 SQL 格式化（Shift+Alt+F）；`Ctrl+Enter`/`Ctrl+Shift+Enter`/`Ctrl+S`/`Ctrl+Shift+P` 快捷键。
- [x] M4-10 EXPLAIN 三引擎（SQLite `EXPLAIN QUERY PLAN` / MySQL `EXPLAIN [ANALYZE]` / PG `EXPLAIN [ANALYZE]`），计划子标签展示。
- [x] M4-11 CSV/JSON 简易导出（前端 Blob 下载，分隔符取 `export.csvDelimiter`）。

**验收**
- [x] 重启恢复标签与 SQL（settings 持久化，CDP 实测写读回）。
- [x] 多语句执行逐语句结果/消息；错误定位到行并高亮（沿用 offset 机制）。
- [x] EXPLAIN 对照 MySQL 8.4.13 与 PostgreSQL 18.0.7 实测（含 ANALYZE 警告）。

> 2026-11-15 v1.0.2 增量（不新增 M 编号）：M4-4 补全升级为关键字 + 表/视图 + 列与 `表名.` / `别名.` 列提示；新增查询标签上下文继承、连接/库下拉与自动切库（§9.7.1）；修复「关闭最后一个标签又弹一个查询页」，改为无标签空态（§9.6）。新增 `tools/test_hint.js` 31 项与 `tools/test_tabs.js` 5 项无浏览器自测。
>
> 2026-11-15 v1.0.3 增量（不新增 M 编号）：补全按整条语句解析 `FROM`（含光标之后、忽略注释）并限定列来源，候选右侧显示所属路径；`tools/test_hint.js` 扩充到 45 项。
> 2026-11-15 v1.0.4 增量（不新增 M 编号）：移除关键字候选右侧的 `keyword` 标签，右侧路径只留给列与表；`tools/test_hint.js` 扩充到 46 项。
- [ ] 20 个标签页切换无卡顿；1MB `.sql` 文件实测；历史 5000 条搜索 < 100ms（未实测）。

### M5 表格数据编辑（8–10 d）→ v0.5.0

> 状态：2026-11-14 已交付并对照真实服务器验收（CDP 探针 + MySQL 8.4.13 / PostgreSQL 18.0.7；CTest `table_ops` 覆盖 SQLite）。

- [x] M5-1 `table.rows`：分页、排序、筛选、搜索、估算总数、`hasMore`。
- [x] M5-2 行标识探测（PK/唯一索引/rowid）+ 只读原因。
- [x] M5-3 网格编辑状态机（insert/update/delete/staged/dirty）。
- [x] M5-4 类型感知编辑器 + NULL 切换 + 校验。
- [x] M5-5 `table.apply`：SQL 预览、事务、回滚、逐行错误。
- [x] M5-6 工具栏：新增/删除/保存/撤销/刷新/精确计数。
- [x] M5-7 筛选栏（操作符/值/AND）+ 服务端查询生成。
- [x] M5-8 单元格查看器：长文本/JSON/图片/HEX/另存。
- [x] M5-9 复制/区域选择/TSV；SQL/INSERT 复制格式。
- [x] M5-10 三引擎 CRUD 集成测试 + 无主键只读测试。
- [ ] M5-11 性能：1000 行暂存修改保存 < 2s（本地）—— 未实测。

**验收**
- [x] 三引擎均可增删改；MySQL/PG 实测插入/更新/删除/`table.apply` 事务 + 回滚（唯一键冲突 `rolledBack:true`，错误定位 `clientIndex`）。
- [x] 无主键表只读且提示原因（`table.rows` 返回 `editable:false` + 原因）；MySQL/PG 均验证。
- [x] 精确计数 `table.count` 与估算 `totalEstimate` 正常返回。
- [ ] 1000 行保存性能未实测；emoji/小数/时间/二进制单元格渲染未经专项验证。

### M6 表设计器与 DDL（8–10 d）→ v0.6.0

> 状态：2026-11-14 已交付并验收。CTest `ddl_builder` 93 项检查通过；`integration_db` 在真实 MySQL 8.4.13 与 PostgreSQL 18.0.7 上完成建表→加载→ALTER→重命名→清空→删除→建库/删库全链路；前端设计器与 Mock 后端经 Node 模拟桥联调通过。SQL 预览已补齐警告、影响行数与新对象名严格校验。

- [x] M6-1 `ddl_builder`：CREATE/ALTER/DROP/RENAME（三引擎）。
- [x] M6-2 表设计器 UI：列/索引/外键/选项/SQL 预览。
- [x] M6-3 从元数据加载现有定义；脏状态检测。
- [x] M6-4 新建表/删除表/重命名/清空。
- [x] M6-5 `ddl.preview`/`ddl.apply` + 警告 + 确认令牌。
- [x] M6-6 视图/函数/触发器 DDL 查看 + 复制/保存。
- [x] M6-7 三引擎 roundtrip 测试：SQLite/MySQL/PostgreSQL 均实跑建父表→建子表（PK/自增/唯一索引/外键）→加载→ALTER→重命名→清空→删除→建库/删库。
- [x] M6-8 MySQL 不可回滚警告；PG 事务；SQLite 限制提示。
- [x] M6-9 数据库创建/删除（MySQL/PG）。
- [x] M6-10 错误恢复：DDL 失败保留设计器状态。

**验收**
- [x] SQLite roundtrip：建父表→建 users（PK/自增/唯一索引/FK）→加载定义→改索引/重命名列/加列/加索引→表重命名→DROP/TRUNCATE，`PRAGMA` 复核一致。
- [x] MySQL/PG 建表 SQL 含主键、唯一索引、外键、注释、表选项（MySQL），ALTER 含改列/加列/删外键/重命名/表选项。
- [x] 删除/清空需输入对象名，令牌不匹配或 DDL 变化后拒绝执行；失败返回 `statementIndex`/`statement`/`rolledBack` 并保留设计器状态。
- [x] 真实 MySQL 8.4.13 / PostgreSQL 18.0.7 上执行 M6 DDL：`integration_db`（`SILKIT_INTEGRATION_WRITE=1`）0 failures。
- [x] SQL 预览满足 §10.4：完整语句（含 schema 限定）+ 影响行数（CREATE `0`、DROP/TRUNCATE 实际 `COUNT(*)`、ALTER 显示“未知”）。
- [x] 预览警告覆盖丢列、缩短长度、改类型、收紧 NOT NULL、新增唯一索引可能失败、MySQL 不可回滚；`ddl_builder` 断言警告文案。
- [x] 新对象名严格校验（§10.6）：UTF-8、长度（MySQL 64 / PostgreSQL 63 / SQLite 255 字节）、控制字符、引号、分号。
- [x] SQLite 文件库夹具 `data/silkit_test.db`（由 `tools/make_test_db.py` 生成）：`sqlite_fixture` 64 项检查通过，覆盖 3 表/1 视图/1 触发器、自增主键、唯一索引、外键级联、只读保护及临时副本上的表/列/索引 DDL。
- [ ] 视图/函数/触发器保存 `.sql` 在原生桥下的文件对话框未经本轮复测（代码链路 `api.pickFile({save:true})` → `app.pick_file` → `app.save_file` 已核对）。

### M7 导入 / 导出 / 转储（5–6 d）→ v0.7.0

- [x] M7-1 `csv` 解析/写出（RFC4180、引号、多行、分隔符探测）。
- [x] M7-2 导出器：CSV/JSON/SQL/Markdown/HTML，流式。
- [x] M7-3 导出向导 UI + 进度 + 取消（`.part` 文件）。
- [x] M7-4 CSV 导入向导：预览/映射/新建表/批量插入/事务。
- [x] M7-5 SQL 文件执行（大文件流式、进度、遇错策略）。
- [x] M7-6 转储：整库/表、结构/数据、DROP IF EXISTS。
- [x] M7-7 编码：UTF-8/UTF-16LE/GBK + BOM。
- [x] M7-8 测试：100 万行导出、10 万行导入、错误回滚。

**实现说明与偏差**
- `src/io/{encoding,csv,exporter,importer,dumper}` 为可复用核心，`ipc/commands_io` 只负责会话、文件与进度；`test_io` 直接覆盖核心逻辑。
- 导出按 64KB 缓冲、按 UTF-8 字符边界切块后转码写出，SQL 每 100 行合并多值 INSERT；100 万行导出在 `test_io` 中以丢弃型 streambuf 验证行数与输出量，内存不随行数增长。
- CSV 导入整文件读入（≤256MB）后解析；批量 INSERT 失败先回滚，再从第 1 行逐条回放定位唯一键等跨批冲突，返回 `record`/`line`/`rolledBack`。
- M7-5 偏差：SQL 文件整文件读入（≤512MB）后经 `SplitSql` 逐条执行，未按块流式拆分；进度、取消、遇错继续均已实现。
- SQL 转储仅覆盖表结构与数据；视图/函数/触发器仍从详情面板另存 `.sql`（与 M6 一致）。

**验收**
- [x] 100 万行 CSV 导出按流式设计，`test_io` 验证 100 万行计数与 >50MB 输出；取消/失败均删除 `.part`。
- [x] 10 万行 CSV 导入成功；注入重复主键后全量回滚，并定位到第 700 条数据 / 文件第 701 行。
- [x] GBK 往返不乱码；UTF-16LE 往返保留 emoji。
- [x] SQLite 整库转储（2 表 3 行）在空库恢复结构与数据，引号内容一致。

### M8 设置、主题、会话恢复与安全（5–6 d）→ v0.8.0

- [x] M8-1 设置页（通用/编辑器/查询/网格/导出/关于）。
- [x] M8-2 五套主题（晨樱/夜樱/薰衣草/奶咖/薄荷）完整覆盖（含 CodeMirror/弹窗/网格），`contrast_check.py` 全部通过。
- [x] M8-3 会话恢复：标签/SQL/分栏/侧栏/滚动位置。
- [x] M8-4 只读连接 + 安全模式 + 危险 SQL 确认。
- [x] M8-5 日志脱敏、崩溃日志、错误上报入口（本地）。
- [x] M8-6 中/英 i18n 覆盖主要界面。
- [ ] M8-7 DPI 125/150/200% 与多显示器；无白闪。
- [x] M8-8 键盘可达性/焦点/IME 检查。
- [x] M8-9 单实例、重复启动激活、配置原子写/备份恢复。

**实现说明与偏差**
- 设置页为单对话框（`frontend/js/views/settings.js`），六个分页；控件通过 `data-path` 与 `settings.json` 对应，保存走 `settings.set` 递归合并，`settings.reset` 恢复默认并保留 `settings.json.bak`。
- 安全模式（`query.safeMode`，默认开启）由后端统一拦截：`query.execute` 与 `query.execute_file` 在拆分语句后调用 `dbutil::CheckSafeMode`，返回 `confirm_required` + `token` + 危险语句列表；前端确认后带令牌重试，令牌随语句变化失效。`DROP/TRUNCATE/ALTER/GRANT/REVOKE/ATTACH/DETACH/VACUUM` 与无 `WHERE` 的 `UPDATE/DELETE` 均视为危险。
- 会话恢复新增：激活标签、编辑器滚动位置、侧栏宽度（可拖拽/键盘调整）与折叠状态；结果网格滚动位置未持久化（偏差）。
- i18n 覆盖顶栏、侧栏、编辑器工具栏、结果标签、状态栏、设置对话框与危险确认；对象树/历史/表设计器/导入导出对话框的动态文案仍以中文为主（偏差，后续逐步补齐）。
- 日志写入统一经过 `MaskSecrets`（password/passwd/pwd/secret/token/api_key 等）；未处理异常写 `logs/crash-*.log`（版本、时间、异常码、地址、线程），不包含连接参数。关于页提供“打开数据目录/日志目录”。
- DPI：TauriCPP 已启用 Per-Monitor V2 与 WebView2 缩放，本轮只做了 100% 与 150% 的手工目视检查，200% 与多显示器切换未留档（M8-7 暂不勾选）。
- 测试：CTest `m8` 覆盖日志脱敏、设置 `.bak` 生成/损坏恢复/重置、危险语句识别与令牌稳定性；`contrast_check.py` 五套主题 125 项全部通过。

**验收**
- 重启后标签/SQL/布局恢复；断开连接标签保留。
- 五套主题切换无白闪；`contrast_check.py` 对正文/次要文本/图标全部达标，输出留档。
- 缩放 200% 下文字/网格/弹窗清晰不模糊。
- 只读连接执行 UPDATE 被后端拒绝；安全模式拦截 DROP 并确认。
- 日志/历史/崩溃文件无密码。

### M9 发布 1.0（3–4 d）→ v1.0.0

> 状态：2026-11-14 已交付发布准备（版本/图标/文档/许可/便携包）；干净虚拟机冒烟与 Git tag 待人工完成。

- [x] M9-1 图标/版本信息/窗口标题/关于页。
- [x] M9-2 README（功能/支持矩阵/构建/限制；截图为偏差项）。
- [x] M9-3 第三方许可声明（libmariadb LGPL、libpq、SQLite、CodeMirror、nlohmann/json、WebView2 SDK）。
- [ ] M9-4 干净 Windows 11 VM 冒烟（有/无 WebView2）：清单已写入 README，待人工在 VM 执行。
- [x] M9-5 三库集成回归 + 手工验收清单。
- [x] M9-6 发布包（portable zip；安装包 v1.1）。
- [x] M9-7 版本号同步检查（CMake/vcpkg/前端/README，`tools/check_version.py`）。
- [ ] M9-8 Git tag + CHANGELOG：CHANGELOG 已完成，`v1.0.0` 标签待提交后创建。

**实现说明与偏差**
- 版本号以 `CMakeLists.txt` 为准，其余位置由 `tools/check_version.py` 校验；`tools/package.ps1` 在打包前强制执行该检查。
- 版本资源 `app_version.rc` 增加 `#pragma code_page(65001)`，修正中文 `FileDescription` 在 exe 属性中乱码的问题。
- 窗口标题改为「Silkit — 跨库数据库工作台」，单实例激活逻辑共用同一标题常量；标题栏/任务栏图标从 exe 内嵌 `app_icon.ico` 加载大小两种尺寸。
- 发布包只含 `Silkit.exe`、`README.md`、`LICENSE`、`THIRD-PARTY-NOTICES.md`；前端资源已内嵌，解压即用。
- 未提供安装包与代码签名（安装包 v1.1，签名待证书）；干净虚拟机冒烟未执行（偏差记录见 §0.2）。

**验收**
- 新机器解压即用；WebView2 缺失有中文引导（框架内已实现，待 VM 复核）。
- 三库连接/查询/编辑/设计/导入导出全流程通过：本机 Release 下 CTest 9/9；MySQL 8.4.13 与 PostgreSQL 18.0.7 的写路径与 DDL 路径集成回归均 0 failures。
- 无 P0/P1 缺陷；已知限制写入 README「已知限制」章节（标题随版本递增）。
- 发布产物：`dist/Silkit-1.0.0-win-x64.zip`（3.83 MB，exe 9.13 MB），exe 版本资源为 `1.0.0`。

---

## 12. 测试与质量

### 12.1 测试环境（2026-11-15 实测基准）

**主基准：本机真实服务**，不是 Docker。E2E 的价值在于拿真实数据 + 真实方言跑通链路。

| 组件 | 版本 / 路径 | 连接参数 | 库与夹具 |
|---|---|---|---|
| MySQL | 8.4.13 · `C:\mysql-8.4.13-winx64\bin\mysql.exe` | `127.0.0.1:3306` · `root` / `123456` | `employees`（30 万员工 / 284 万薪资 / 9 部门，含 `enum`、`date`、外键、视图）；另有 `silkit_test`（`t_demo`） |
| PostgreSQL | 18.7 · `C:\Program Files\PostgreSQL\18\bin\psql.exe`（服务 `postgresql-x64-18`） | `127.0.0.1:5432` · `postgres` / `123456` · schema `public` | `employees`（`tests/e2e/seed_pg_employees.sql` 精简镜像：9 部门 / 12 员工 / 18 薪资行 / 视图 / 外键）；`silkit_test` 为自动回退库 |
| SQLite | 3.4x（vcpkg 静态库） | 文件 / 内存 | `data/silkit_test.db`（`tools/make_test_db.py` 生成：自增主键、唯一/普通索引、外键、BLOB、视图、触发器） |
| 构建 | VS 2022 Enterprise（`/std:c++20`）、C++20、CMake 3.21+、vcpkg、Python 3 | `build.ps1` / `tools/e2e_runner.py` | `build/Release` |

> **环境事实核对**：初次盘点时系统 PG **没有** `employees` 库（只有 `postgres` / `silkit_test` / `template0` / `template1`）。现已通过 `tools/e2e_runner.py --seed-pg-employees` 补建同名同结构精简镜像；若他人的机器上没有该库，runner 会自动退回 `silkit_test` 并打印 WARN，不会假装通过。MySQL 的 `employees` 是完整标准示例库，PG 侧刻意只放几十行，用于「大表 vs 小表」的跨引擎对照。

**环境变量（与代码一致，均为分项变量，没有 DSN 字符串）**

| 变量 | 作用 |
|---|---|
| `SILKIT_MYSQL_HOST/PORT/USER/PASSWORD/DATABASE` | 设置 `HOST` 才启用 MySQL 组；未设置整组 SKIP |
| `SILKIT_PG_HOST/PORT/USER/PASSWORD/DATABASE/SCHEMA` | 设置 `HOST` 才启用 PG 组；`SCHEMA` 默认 `public` |
| `SILKIT_INTEGRATION_WRITE=1` | 放开 `integration_db` 的 M5 / M6 写路径（影子表/库） |
| `SILKIT_E2E_WRITE=1` | 放开 E2E-11 的 CSV 导入影子表 |
| 二者都不设置 | 全部为只读用例；适合接入生产库的冒烟 |

**CI 备选（Docker，非本机基准）**

```yaml
services:
  mysql:
    image: mysql:8.4
    environment:
      MYSQL_ROOT_PASSWORD: 123456
      MYSQL_DATABASE: silkit_test
    ports: ["3306:3306"]
    command: --character-set-server=utf8mb4 --collation-server=utf8mb4_0900_ai_ci
  postgres:
    image: postgres:18
    environment:
      POSTGRES_PASSWORD: 123456
      POSTGRES_DB: silkit_test
    ports: ["5432:5432"]
```

CI 上把 `SILKIT_MYSQL_DATABASE` / `SILKIT_PG_DATABASE` 指向 `silkit_test` 即可；E2E-07 / E2E-12 在缺表时会自动使用等价数据，不依赖 `employees` 大表。

**构建注意**：本机环境同时存在 `HTTPS_PROXY`/`https_proxy`（及 HTTP 大小写两份），MSBuild 的 CL 任务会因 .NET 进程环境变量字典大小写不敏感抛 `MSB6001: 已添加项`。`tools/e2e_runner.py` 已在构建子进程中剔除小写重复键；手工构建可用 `env -u https_proxy -u http_proxy cmake --build build --config Release`。

### 12.2 单元测试（CTest，不引第三方框架）

按 `tests/CMakeLists.txt` 的**实际 10 个目标**回写（此前表里的 `test_sql_splitter` / `test_identifier` / `test_type_map` / `test_csv` 从未落地，纯逻辑检查被合并进下列目标）：

| # | CTest 目标 | 覆盖 |
|---|---|---|
| 1 | `silkit_smoke` | SQL 分割（引号/注释/`$tag$`/`DELIMITER`/偏移）、标识符引用转义、类型映射、基础工具 |
| 2 | `sqlite_driver` | SQLite 内存库 CRUD、NULL、BLOB、取消、事务 |
| 3 | `engine_dialect` | 三引擎方言 SQL、类型/错误映射 |
| 4 | `table_ops` | 筛选/排序/分页 SQL 生成、参数顺序、变更语句构造 |
| 5 | `ddl_builder` | CREATE/ALTER 快照对比、三引擎 DDL 断言 |
| 6 | `sqlite_fixture` | `data/silkit_test.db` 文件夹具：索引/外键/视图/触发器元数据 |
| 7 | `io` | RFC4180 CSV（多行/引号/BOM/GBK）、五种格式流式导出、100 万行导出、10 万行导入、错误回滚 |
| 8 | `m8` | 设置合并/备份恢复、日志脱敏、安全模式、崩溃日志、会话恢复（25 项） |
| 9 | `integration_db` | 真实 MySQL/PG 连接、M5 CRUD、M6 DDL 全链路（`SILKIT_INTEGRATION_WRITE=1` 时） |
| 10 | `e2e_real_db` | §12.4 的 14 项 × 双引擎真实库用例矩阵 |

**前端逻辑自测（非 CTest）**：`node tools/test_hint.js`（46 项）覆盖智能补全的表/视图候选、`表名.` / `别名.` 列提示、`FROM` 表列（含 FROM 位于光标之后、别名、多语句与注释隔离）、按库隔离、缓存失效、PG schema 透传、候选右侧所属路径、关键字无标签与自定义 `render`；`node tools/test_tabs.js`（5 项）覆盖启动无标签不建查询页、关闭最后一个标签停在空态、反复开关、关闭非激活标签不误切换；`node --check` 校验全部前端 JS。前端交互 E2E 仍按 §12.4.2 待接入 Playwright。

**要求**：核心纯逻辑覆盖率 ≥ 70%；新增解析/生成逻辑必须带测试；新增跨引擎行为必须同时加真实库 E2E 用例（避免「单测全绿、真库出错」——MySQL 数值列误判二进制与 PG 空表丢列头都是这样漏掉的）。

### 12.3 集成测试

- 三引擎连接、元数据、查询、取消、事务、CRUD、DDL、导入导出。
- 类型矩阵：整数边界（BIGINT 64）、DECIMAL(38,10)、DOUBLE、布尔、日期时间（含微秒/时区）、UTF-8 emoji、BLOB 1MB、JSON 嵌套、PG 数组/UUID、MySQL BIT。
- 错误矩阵：错误密码、库不存在、语法错误、唯一键冲突、外键冲突、权限不足、连接中断。
- 大数据：10 万/100 万行分页与导出。

> 2026-11-15 回写：`integration_db` 覆盖连接 + M5/M6 写路径；上表里的类型矩阵、错误矩阵、大数据已从「描述」落成 `e2e_real_db` 的可断言用例（§12.4.1）。100 万行导出 / 10 万行导入由 `io` 单测覆盖，不再只写在计划里。

### 12.4 UI / E2E（2026-11-15 重排）

三层分工：**① 真实库后端 E2E**（已自动化，CTest）/ **② 前端交互 E2E**（Playwright + `frontend/js/mock/mock.js`，待接入）/ **③ WebView2 特有行为**（手工清单）。

#### 12.4.1 自动化：真实库 E2E（`tests/e2e/e2e_real_db.cpp`）

每台引擎 14 项，MySQL `employees` + PG `employees` 各跑一遍，共 **31 项**；另有一组可选的「服务端不支持 TLS」回归 **3 项**（`SILKIT_MYSQL_NOSSL_HOST` 启用）。未设置 `*_HOST` 时对应整组 SKIP 且不计失败。

| 编号 | 用例 | 数据源 | 验收点 | 守护的 Gap / 历史缺陷 |
|---|---|---|---|---|
| E2E-01 | 连接握手/版本/能力 | MySQL + PG | `Ping`、版本、当前库、占位符 `?`/`$1`、cancel/explain 能力位 | §19 A1 驱动能力矩阵；PG 版本号 18.0.7 缺陷 |
| E2E-02 | 错误密码被拒绝 | MySQL + PG | 拒绝连接且错误信息非空；trust 认证环境记 SKIP 不误报 | 错误矩阵；凭据处理 |
| E2E-03 | 不存在的库被拒绝 | MySQL + PG | 连接失败且带原生错误码 | 错误矩阵 |
| E2E-04 | 错误可读 + 方言语句分割 | MySQL + PG | 语法/对象错误有 code+message；引号/`;`/注释/`$tag$` 内不误切，3 条语句 | §19 D8 语义诊断 / SQL 分割双实现一致性 |
| E2E-05 | 对象树/列元数据 | MySQL + PG | `information_schema` 可读、首表 `SELECT *` 返回列头（**含 0 行表**） | 新发现并修复：PG 空结果丢列头 |
| E2E-06 | 类型矩阵/数值非二进制/UTF-8 | MySQL + PG | `9223372036854775807` 不被 base64、DECIMAL 精度、NULL、emoji 原样 | M1–M4 MySQL `charsetnr==63` 潜伏 bug 防回归 |
| E2E-07 | 5000 行流式分批读取 | MySQL `salaries` + PG `generate_series` | 5000 行、≥50 批、首批带列元数据、耗时记录 | §12.6 性能预算；§19 E1 虚拟网格后端 |
| E2E-08 | `maxRows` 截断标记 | MySQL + PG | 恰好 37 行且 `truncated=true` | §6.7 行数上限；§19 E2 背压前置 |
| E2E-09 | 取消后连接可恢复 | MySQL + PG | `Cancel()` 后 `SELECT 1` 仍可用 | §19 E3 取消/慢查询 |
| E2E-10 | 只读连接拦截写语句 | MySQL + PG | `SetReadOnly(true)` 后 `CREATE TABLE` 在驱动层被拒，恢复可写 | §19 B7 只读连接；安全模式 |
| E2E-11 | CSV 导出/解析/导入回环 | MySQL + PG | 带逗号/引号/中文/空值 → `ExportWriter` → `CsvParser` → `ImportCsv` 影子表 → 3 行回读 → DROP | §19 G1 XLSX/多格式之前的基线回归 |
| E2E-12 | 真实数据聚合正确性 | MySQL + PG `departments` | 部门数 = 9，跨引擎一致 | dev-story 的 30 万员工验收脚本化 |
| E2E-13 | 语句分割与错误定位偏移 | MySQL + PG | 2 条语句且偏移递增 | §19 D8 前端/后端分割语义一致 |
| E2E-14 | `EXPLAIN` 执行计划 | MySQL + PG | 返回计划行且无错误 | §19 I4 计划可视化的数据前置 |

> 写路径默认关闭；`SILKIT_E2E_WRITE=1` 时 E2E-11 建删 `silkit_e2e_csv_probe`，`SILKIT_INTEGRATION_WRITE=1` 时 `integration_db` 建删 `silkit_m5_probe` / `silkit_m6_*` / `silkit_m6_db_probe`。均为独立影子对象，不触碰业务表。

**可选：无 TLS 的 MySQL 协议服务端**（本机 3307 SQLTiny，握手 capability 不含 `CLIENT_SSL`）。由 `SILKIT_MYSQL_NOSSL_HOST/PORT/USER/PASSWORD` 或 `e2e_runner.py --mysql-nossl-host` 启用；未设置时整组 SKIP。用于守住 MariaDB Connector/C 3.4 的 SSL 语义回归（§0.2）。

| 编号 | 用例 | 验收点 |
|---|---|---|
| SSL-01 | `prefer` 自动降级为明文 | 连接成功且 `SELECT 1` 返回 1 行 |
| SSL-02 | `disable` 强制明文连接 | 连接成功且 `SELECT 1` 返回 1 行 |
| SSL-03 | `require` 拒绝无 TLS 服务端 | 连接失败、code=2026、hint 提示改用 `disable` |

#### 12.4.2 前端交互 E2E（Playwright + `mock.js`，待接入）

| 编号 | 用例 | 方式 | 验收点 | Gap 依据 |
|---|---|---|---|---|
| U-01 | 连接树展开 / 过滤 / 右键菜单 | Playwright + mock | 懒加载节点、`meta.search` 与本地过滤一致 | §19 C1 远程搜索（当前后端先行、前端未接） |
| U-02 | 编辑器执行当前/选中/全部、多结果子标签、错误高亮 | Playwright + mock | 执行范围与后端 `SplitSql` 语义一致 | §19 D1/D8 |
| U-03 | 结果网格虚拟滚动、列宽、排序、复制（TSV/CSV/JSON/INSERT） | Playwright + mock | 10 万行不卡、复制值不丢精度 | §19 E4 复制为 SQL |
| U-04 | 表格数据 CRUD、保存预览、事务回滚、唯一键冲突 | Playwright + mock | 冲突后整体回滚、单元格定位 | §19 E5 变更预览 |
| U-05 | 表设计器增删列 / SQL 预览 / 影响行数 / 令牌确认 | Playwright + mock | 危险操作二次确认，SQL 与执行同源 | §19 F1 设计器深度 |
| U-06 | CSV 导入向导（编码/分隔符/列映射/错误行号） | Playwright + mock | 错误定位到第 N 行、事务回滚 | §19 G2 导入向导 |
| U-07 | 五套主题 / 中英文切换 / 对比度 | Playwright + `tools/contrast_check.py` | 无白闪、125 项对比度达 4.5、CodeMirror/网格同步 | §19 J2 i18n 覆盖度 |
| U-08 | 安全模式：`DROP`/`TRUNCATE`/无 `WHERE` 的 `UPDATE` 拦截 | Playwright + mock | 取消后消息区明确提示且未执行 | dev-story 手工项自动化 |
| U-09 | 会话恢复：标签/SQL/滚动/侧栏 | Playwright + mock | 重启后激活标签与内容还原 | §19 J3 会话恢复完整性 |
| U-10 | 断线/重连：状态栏、标签保留 | Playwright + mock | 8s 内返回、可重试 | §19 B6 自动重连 |
| U-11 | 快捷键全流程（除拖拽外无鼠标） | Playwright | 全流程可达 | §12.5 手工项自动化 |
| U-12 | 大结果集滚动 / 取消 / 继续加载 | Playwright + mock | 首屏 < 1.5s、取消 < 500ms | §12.6 性能预算 |
| U-13 | 连接失败错误条「复制信息」 | 手工 / Playwright + mock（构造 2026、1045 等错误） | 错误条最右侧按钮与错误 toast 均可复制；剪贴板含错误码/SQLSTATE/hint 与连接上下文且**不含密码**；按钮反馈「已复制」 | §0.2 2026-11-15 偏差记录 |
| U-14 | 新建查询继承对象树选中库并自动 `USE` | Playwright + mock | 从库/表节点新建的标签工具条显示该库；查询上下文与选中节点一致 | §9.7.1 |
| U-15 | 查询页连接 / 库下拉与多标签上下文隔离 | Playwright + mock | 切库即时生效、补全候选随库变化；A/B 标签互不污染，激活 / 执行自动切回本标签的库 | §9.7.1 |
| U-16 | 智能补全（关键字 / 表 / 列 / `表名.` / `别名.` / `库名.`） | `node tools/test_hint.js` 或 Playwright + mock | 点号后只出该表列、候选带类型、替换区间不吞掉表名前缀；未知表名返回空不串列；FROM 后 `库名.` 出表名；切库后候选刷新；SELECT 列表在前、FROM 在后时仍能提示该表列；候选右侧显示所属路径 | §9.7 |
| U-17 | 无标签空态与彻底关闭 | `node tools/test_tabs.js` 或 Playwright + mock | 启动无标签不建查询页；关闭最后一个标签后停在空态且不会再弹新页；重启后仍保持关闭；空态按钮可新建查询 / 打开 SQL | §9.6 / §0.2 |

> 现状：`frontend/js/mock/mock.js`（2259 行）已能模拟 `ipc.call`/`ipc.event` 与流式查询，但仓库没有 Playwright 依赖与 CI 任务。按 §19 判定为 **P1**，先用 §12.5 手工矩阵兜底；接入时复用 mock 数据，不得连真实库跑 UI 用例。

#### 12.4.3 WebView2 特有 / 手工

拖放 `.sql`/`.db`/`.csv`、原生文件对话框、125/150/200% DPI 与多显示器、窗口关闭时未保存确认与事务提示、干净虚拟机冷启动，走 §12.5 手工验收矩阵并留档。

### 12.5 手工验收矩阵

| 场景 | 检查点 |
|---|---|
| 首次启动 | 无白屏、欢迎页、版本号 |
| 连接失败 | 8s 内返回、错误可读、可重试 |
| 断网/恢复 | 状态栏断线、重连、标签保留 |
| 大结果 | 滚动流畅、取消、继续加载 |
| 编辑冲突 | 唯一键冲突回滚、单元格定位 |
| DDL | 预览/警告/执行/刷新 |
| 导入错误 | 行号、回滚、日志 |
| 编码 | GBK/UTF-8/emoji/中文路径 |
| 高 DPI | 125/150/200%、多显示器 |
| 主题 | 5 套切换无白闪、对比度达标、CodeMirror/网格同步 |
| 键盘 | 全流程无鼠标可用（除拖拽） |
| 查询上下文 | 树选库 → 新建查询自动 `USE`；下拉切库即时生效；多标签互不污染 |
| 智能补全 | 关键字 / 表 / 列命中，`表名.` 与别名只出该表列，切库后候选刷新 |
| 无标签空态 | 关闭最后一个标签后不反弹查询页；空态按钮与 `Ctrl+N` 可重新打开 |
| 错误诊断复制 | 连接失败错误条 / toast 复制内容含错误码与上下文、不含密码 |
| 确认弹窗 | 删除连接等 13 处均为主题化弹窗，无系统白框 |
| 退出 | 未保存确认、事务提示、无挂起 |

### 12.6 性能预算

| 指标 | 目标 |
|---|---|
| 冷启动到可交互 | < 1.5s（WebView2 已安装） |
| exe 体积 | < 35MB |
| 空闲内存 | < 200MB（无结果集） |
| 10 万行结果 | 首屏 < 1.5s，滚动 ≥ 55fps，内存 < 350MB |
| 5000 表元数据 | < 1.5s |
| 取消查询 | < 500ms 反馈 |
| 1000 行保存 | < 2s（本地） |
| 100 万行 CSV 导出 | 内存增量 < 100MB，可取消 |
| 标签切换 | < 100ms（20 标签） |

### 12.7 Definition of Done（每个任务）

- [ ] 代码通过 `/W4` 编译（新增代码零警告）。
- [ ] 关键逻辑有单元/集成测试；手工路径已走通。
- [ ] 命令/参数/事件已更新到本文 §8。
- [ ] 错误路径有可读提示；无 `alert()`/静默失败。
- [ ] 不泄露密码；日志已脱敏。
- [ ] 三库（或声明的子集）验证通过。
- [ ] 中文文案已入 i18n；英文不缺 key。
- [ ] 性能预算未被突破。
- [ ] 版本号/CHANGELOG 已更新（里程碑末尾）。

### 12.8 CI

```yaml
# .github/workflows/build.yml（规划）
- windows-latest
- 缓存 vcpkg（key: vcpkg-<hash(vcpkg.json)>）
- cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static
- cmake --build build --config Release
- ctest -C Release --output-on-failure
- python tools/contrast_check.py --fail-under 4.5
- 上传 build/Release/Silkit.exe 与测试日志
```

- 集成测试在 CI 可选（service containers 仅 Linux；Windows runner 用 `docker compose` 或跳过）。
- 发布 tag 触发构建 + zip + SHA256。
- 2026-11-15：Windows self-hosted runner 若已按 §12.1 装好 MySQL/PG，追加 `python tools/e2e_runner.py --no-build --full`，报告 `build/e2e-report.md` 作为 CI artifact；无真实服务时只跑 1–8 号纯逻辑 CTest（9/10 号自动 SKIP）。

### 12.9 E2E 执行入口（2026-11-15 新增）

```powershell
# 默认：探测两个测试源 → 增量构建 → 跑 integration_db + e2e_real_db → 写报告
python tools/e2e_runner.py

# 全量 CTest（10 个目标）
python tools/e2e_runner.py --full

# 只读冒烟：关闭所有影子表写入，适合指向生产库
python tools/e2e_runner.py --read-only

# 首次准备 PG employees 镜像库（CREATE DATABASE + 建表导数据；已存在则跳过，不覆盖）
python tools/e2e_runner.py --seed-pg-employees

# 可选：额外跑「服务端不支持 TLS」的 SSL-01~03（本机 3307 SQLTiny）
python tools/e2e_runner.py --mysql-nossl-host 127.0.0.1 --mysql-nossl-port 3307 `
    --mysql-nossl-user root --mysql-nossl-password 123456

# 指定库 / 指定可执行文件路径（换机器时用）
python tools/e2e_runner.py --mysql-db silkit_test --pg-db silkit_test `
    --mysql-bin "C:\mysql-8.4.13-winx64\bin\mysql.exe" `
    --psql-bin "C:\Program Files\PostgreSQL\18\bin\psql.exe"

# 手工等价入口（不经过 runner，注意清理大小写重复的代理变量）
$env:SILKIT_MYSQL_HOST="127.0.0.1"; $env:SILKIT_MYSQL_USER="root"
$env:SILKIT_MYSQL_PASSWORD="123456"; $env:SILKIT_MYSQL_DATABASE="employees"
$env:SILKIT_PG_HOST="127.0.0.1"; $env:SILKIT_PG_USER="postgres"
$env:SILKIT_PG_PASSWORD="123456"; $env:SILKIT_PG_DATABASE="employees"
ctest --test-dir build -C Release -R "integration_db|e2e_real_db" -V
```

`tools/e2e_runner.py` 做四件事：① 用 `mysql.exe`/`psql.exe` 预检版本与目标库，PG 有 `employees` 就优先用，否则退回 `silkit_test` 并告警；② 清理 MSBuild 的大小写重复代理变量后增量构建 `e2e_real_db` / `integration_db`；③ 注入 §12.1 环境变量并跑 CTest（`-V` 保留逐项输出）；④ 把 PASS/FAIL/SKIP 统计与原始输出写入 `build/e2e-report.md`。密码通过 `MYSQL_PWD` / `PGPASSWORD` 传递，不出现在命令行与报告里。

**当前基线（2026-11-15 实测）**：MySQL 8.4.13 `employees` + PostgreSQL 18.7 `employees`，`ctest -C Release` **10/10 通过**；`e2e_real_db` 基础 **28 passed**，加上 `--mysql-nossl-host` 的无 TLS 组共 **31 passed / 0 failed / 0 skipped**；`integration_db` 双引擎 M5/M6 写路径全绿，报告 `build/e2e-report.md`。

---

## 13. 风险与对策

| # | 风险 | 概率 | 影响 | 对策 |
|---|---|---|---|---|
| R1 | libmariadb/libpq 静态链接与 `/MT` 冲突、缺系统库 | 中 | 高 | **M0 先做链接 spike**；用 vcpkg 静态 triplet；准备动态 DLL 回退方案 |
| R2 | libpq 静态 OpenSSL 证书/Provider 问题 | 中 | 高 | 用 `verify-ca/full` 实测；必要时随包 `openssl.cnf` 或改动态 |
| R3 | MySQL 8.4 认证插件（caching_sha2）失败 | 中 | 高 | M2 首日实测无 SSL/有 SSL；失败则提示启用 SSL 或改用户插件 |
| R4 | 驱动对象跨线程使用导致崩溃 | 中 | 高 | Session 线程亲和 + 断言；`Cancel` 单独设计；代码评审 |
| R5 | 取消后连接状态损坏 | 高 | 中 | 排空结果；失败则关闭并自动重连；UI 明示“连接已重置” |
| R6 | 超大结果/大字段撑爆内存 | 高 | 高 | 分批 + maxRows + 单元格截断 + 按需拉取 + 背压 |
| R7 | SQL 分割器边界错误 | 中 | 中 | 单元测试覆盖 dollar quote/注释/DELIMITER；错误时回退整段执行 |
| R8 | PG DDL 重建不完整 | 高 | 中 | 明确标注；提供原始 `pg_get_*def`；v1.1 补分区/权限/表空间 |
| R9 | JS 大整数精度丢失 | 高 | 中 | 全字符串传输；禁止 `Number()` 转换 ID/DECIMAL |
| R10 | MySQL DDL 自动提交导致误操作 | 中 | 高 | 预览 + 红色警告 + 输入表名确认 |
| R11 | WebView2 缺失/被策略禁用 | 中 | 高 | 启动检测 + 中文引导 + 离线安装包链接；提供“复制下载地址” |
| R12 | 杀软误报/未签名 exe | 中 | 中 | 不打包 UPX；后续代码签名；发布 SHA256 |
| R13 | LGPL 合规 | 中 | 中 | 静态链接时提供目标文件/重链接说明；或改动态 DLL；发布前法务确认 |
| R14 | 多实例写配置冲突 | 低 | 中 | 单实例互斥体；原子写 + `.bak` |
| R15 | 时区/字符集显示歧义 | 中 | 低 | 保留服务端原文；状态栏显示会话时区/字符集；文档说明 |
| R16 | 表格无主键不可编辑 | 高 | 低 | 明确只读原因；SQLite rowid 例外；v1.1 支持自定义行标识 |
| R17 | 深分页慢 | 高 | 低 | 提示 + 精确计数按需；v1.1 keyset 分页 |
| R18 | 范围蔓延（AI/ER/同步） | 高 | 高 | 本文“暂不做”清单；v1 只保 P0 |

---

## 14. 版本路线图

| 版本 | 内容 | 对应 |
|---|---|---|
| v0.1 | SQLite 可用（连接/树/查询/结果） | M1 |
| v0.2 | + MySQL/MariaDB | M2 |
| v0.3 | + PostgreSQL | M3 |
| v0.4 | 查询工作区完整 | M4 |
| v0.5 | 表数据编辑 | M5 |
| v0.6 | 表设计器/DDL | M6 |
| v0.7 | 导入导出/转储 | M7 |
| v0.8 | 设置/主题/会话/安全 | M8 |
| **v1.0** | Windows 单文件正式版 | M9 |
| **v1.0.1** | 维护版：SSL 语义修复、连接失败诊断复制、13 处主题化确认框、M8 徽章清理、C++20、真实库 E2E 矩阵与英文 README | §0.2 |
| **v1.0.2** | 体验增强：新建查询自动 `USE`、查询页连接/库下拉与标签上下文、关键字 + 表/视图 + 列智能补全（`表名.` / `别名.`）、无标签空态修复、两个无浏览器自测 | §9.6 / §9.7.1 |
| **v1.0.3** | 补全修复：按整条语句解析 FROM 表限定列（含光标之后、忽略注释）、候选右侧显示所属路径、去掉 `(table)` / `(view)` 后缀、补全自测 45 项 | §9.7 / §9.7.1 |
| **v1.0.4** | 展示优化：关键字候选移除右侧 `keyword` 标签、补全自测 46 项 | §9.7 / §9.7.1 |
| **v1.1**（P0，日常完成度） | XLSX 导入/导出、结果复制为 INSERT/UPDATE、ER 图（只读、基于现有 FK 元数据）、EXPLAIN 计划树、连接配置导入导出、查询历史落 `history.jsonl`、断线自动重连、i18n 补齐动态视图、DML 只读预览、前端 Playwright E2E | §19 P0 |
| **v1.2**（P1，专业增强） | SSH 隧道/代理、SSL 证书 UI、Schema Diff 只读对比、数据对比（按主键）、查询结果图表、单元格值格式化/掩码、模拟数据生成、会话/锁等待面板、深分页 keyset、SQL 审计中心（本地） | §19 P1 |
| **v2.0**（P2，平台化，需重新评估定位） | 数据传输/同步、定时备份、SQL Server/Oracle 等新驱动、插件 SDK、MCP Server、AI SQL 助手（显式授权、默认不执行） | §19 P2 |
| **明确不做**（与「单 exe / 离线 / 无账号 / <10MB」冲突） | 100+ 异构数据源、Web Server / Docker / CLI 形态、云同步与团队账号、插件市场与签名沙箱、后台常驻调度进程、Win7 兼容构建 | §19「不做」 |

> 2026-11-15 重排依据：原 v1.1 把「SSH、ER 图、Schema Diff、XLSX、用户管理」混在一起，没有区分「开发者每天会碰到」与「DBA 平台能力」。按 §19 对标后，**XLSX / 复制为 SQL / ER 图 / 计划可视化 / 历史与重连** 属于完成度底线（P0）；SSH、Schema Diff、同步属于专业增强（P1/P2）；用户权限管理、云/Web/MCP 与轻量定位冲突，降级或明确不做。E2E 用例（§12.4）优先覆盖 P0 涉及的既有链路回归。
>
> v1.0.1 ~ v1.0.4 是 patch 增量（缺陷修复 + 体验增强），不占用 v1.1 里程碑编号；四者均已随 `tools/check_version.py` 同步 8 处版本标识并产出便携包。

**v1.1 预留接口**
- `DriverRegistry::Register(std::unique_ptr<Driver>)`：新增数据库不动 UI。
- `ConnectParams.extra`：SSH/代理/驱动参数。
- `Capabilities`：UI 按能力隐藏入口。
- `ipc` 命令版本化：`args.apiVersion`，后端兼容旧参数。

---

## 15. 开工清单（Day 0–Day 3）

### Day 0：依赖与链接验证（最大风险先做）

```powershell
# 1. 安装依赖（首次约 20–40 分钟）
C:\vcpkg\vcpkg install libmariadb:x64-windows-static --classic
C:\vcpkg\vcpkg install libpq:x64-windows-static --classic
C:\vcpkg\vcpkg install sqlite3:x64-windows-static --classic

# 2. 验证头文件/库
Test-Path C:\vcpkg\installed\x64-windows-static\include\mysql.h
Test-Path C:\vcpkg\installed\x64-windows-static\include\libpq-fe.h
Test-Path C:\vcpkg\installed\x64-windows-static\include\sqlite3.h
```

写一个 30 行的 `spike.cpp`：分别 `mysql_init/mysql_real_connect`、`PQconnectdb`、`sqlite3_open` 到本地/空连接，确认能编译链接运行（不要求连上）。

### Day 1：骨架

```powershell
cd D:\Work\CPlusPlus\Silkit
robocopy D:\Work\CPlusPlus\StepDraw\TauriCPP .\TauriCPP /E /XD build .git
# 创建 CMakeLists.txt / build.ps1 / vcpkg.json / src / frontend
powershell -ExecutionPolicy Bypass -File build.ps1 -DevMode
```

- [ ] 窗口打开，晨樱浅色背景，F12。
- [ ] `app.info`/`app.ping` 往返。
- [ ] 前端外壳可见。
- [ ] 顶栏主题切换器可用：5 套主题 + `Ctrl+Shift+T` / `Ctrl+Shift+L`。
- [ ] 切主题后重启仍保持，无白闪。

### Day 2–3：异步 IPC + SQLite 连接

- [ ] `AsyncRouter` 完成，进度/取消可用。
- [ ] `SqliteConnection` 打开内存库，执行 `SELECT 1`。
- [ ] 前端点击“打开 SQLite” → 文件对话框 → 树显示 `sqlite_master`。
- [ ] 结果网格显示 3 行测试数据。

> 完成 Day 3 即可判定：**架构可行，进入 M1**。

---

## 16. 附录

### 16.1 CMake 关键片段

```cmake
cmake_minimum_required(VERSION 3.21)
project(Silkit VERSION 1.0.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus)
  add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

# TauriCPP：沿用 StepDraw 顶层直接编译静态库的方式，避免改框架 CMake
set(TAURICPP_DIR "${CMAKE_SOURCE_DIR}/TauriCPP" CACHE PATH "TauriCPP root")
file(GLOB TAURICPP_SOURCES CONFIGURE_DEPENDS "${TAURICPP_DIR}/src/*.cpp")
add_library(tauricpp STATIC ${TAURICPP_SOURCES})
target_include_directories(tauricpp PUBLIC
  "${TAURICPP_DIR}/include"
  "${TAURICPP_DIR}/third_party"
  "${WEBVIEW2_INCLUDE_DIR}"
)
target_link_libraries(tauricpp PUBLIC
  shlwapi shell32 comdlg32 ole32 ${WEBVIEW2_LIBRARY})

find_package(unofficial-libmariadb CONFIG REQUIRED)
find_package(PostgreSQL CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_executable(Silkit WIN32
  src/main.cpp
  # ... 源文件
  "${RESOURCES_RC}" "${RESOURCE_MAP_CPP}" ${APP_ADDITIONAL_SOURCES}
)

target_link_libraries(Silkit PRIVATE
  tauricpp
  unofficial::libmariadb
  PostgreSQL::PostgreSQL
  unofficial::sqlite3::sqlite3
  crypt32 ws2_32 secur32 shell32 shlwapi ole32 comdlg32
)
```

> 注：TauriCPP 自带的 `CMakeLists.txt` 是**顶层可执行项目**，直接 `add_subdirectory` 会与 Silkit 的 `project()`/资源目标冲突。M0 采用 StepDraw 的做法：在项目顶层 `add_library(tauricpp STATIC ...)`，只复用 `include/src/third_party`，不改框架源码。WebView2 的 `find_path/find_library` 片段见 StepDraw 的 `CMakeLists.txt`，原样搬入。

### 16.2 vcpkg.json

```json
{
  "name": "silkit",
  "version-string": "1.0.4",
  "dependencies": [
    "libmariadb",
    "libpq",
    "sqlite3",
    "webview2"
  ]
}
```

- `webview2` 仍按 StepDraw 的 `find_path/find_library` 方式使用（vcpkg 的 WebView2 不提供统一 CMake config）。
- 构建命令加 toolchain：
  ```powershell
  cmake -B build -S . `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static
  ```

### 16.3 .clang-format

```yaml
BasedOnStyle: Microsoft
Language: Cpp
Standard: c++17
ColumnLimit: 110
IndentWidth: 4
UseTab: Never
BreakBeforeBraces: Allman
PointerAlignment: Left
AllowShortFunctionsOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: Never
SortIncludes: CaseSensitive
```

### 16.4 日志规范

```
2026-11-13 10:12:31.482 [I] [ui] app ready version=1.0.0
2026-11-13 10:12:35.117 [I] [s:7f3a] conn.open engine=mysql host=127.0.0.1:3306 user=root
2026-11-13 10:12:35.155 [I] [s:7f3a] conn.open ok server=8.4.3 elapsed=38ms
2026-11-13 10:12:41.902 [W] [q:r12] query.cancel requested
2026-11-13 10:12:42.011 [E] [q:r12] query.error code=1317 message="Query execution was interrupted"
```

- 密码/令牌替换为 `***`；连接串只记录 host/port/user。
- 单文件 5MB 轮转；保留 5 个；默认 INFO，设置可开 DEBUG。

### 16.5 命令与事件命名索引

| 前缀 | 域 |
|---|---|
| `app.` | 应用生命周期/信息/URL |
| `settings.` | 设置 |
| `ui_state.` | 会话恢复 |
| `theme.` | 主题应用、窗口底色同步 |
| `conn.` | 连接配置与运行时 |
| `meta.` | 对象树/详情/DDL |
| `query.` | SQL 执行/历史/片段/文件 |
| `table.` | 表数据/计数/保存/单元格 |
| `ddl.` | 建表/改表/删除/重命名 |
| `io.` | 导入/导出/转储/作业 |
| `ipc.` | 异步路由/取消/事件 |

事件：`ipc.event`（type: `batch|progress|result|error|log`）、`session.closed`、`file-dropped`（框架内置）。

### 16.6 参考资料

- StepDraw / TauriCPP 源码：`D:\Work\CPlusPlus\StepDraw`
- HeidiSQL 源码：`D:\Work\CPlusPlus\Silkit\HeidiSQL-12.22`（dbstructures.mysql/postgresql/sqlite、table_editor、grideditlinks 可参考行为与元数据查询）
- GoNavi：https://github.com/Syngnat/GoNavi（Wails + React，多数据源/驱动管理/大数据网格）
- DBX：https://github.com/t8y2/dbx（单二进制/AI/MCP/CodeMirror/虚拟网格）
- MariaDB Connector/C：https://mariadb.com/kb/en/mariadb-connector-c/
- libpq：https://www.postgresql.org/docs/current/libpq.html
- SQLite C API：https://www.sqlite.org/c3ref/intro.html
- CodeMirror 5：https://codemirror.net/5/

### 16.7 待定事项（开工会拍板）

| # | 事项 | 建议 | 截止 |
|---|---|---|---|
| Q1 | 目录与命名已统一为 **Silkit**（`D:\Work\CPlusPlus\Silkit`、`Silkit.exe`、`silkit::`、`%APPDATA%\Silkit`），是否保留 `PG_DIAG_*` 等第三方 API 字样 | 保留：libpq 宏名不可改；其余业务标识不再出现旧名 | M0 |
| Q2 | 许可证 | 内部工具可用 MIT；对外发布前评估 libmariadb LGPL | M0 |
| Q3 | 是否要 SSH 隧道进 v1 | 建议 v1.1；v1 先支持 SSL | M2 |
| Q4 | XLSX 导出优先级 | v1.1（需 zip/xml 写入器） | M7 |
| Q5 | 是否支持多实例 | 默认单实例；专业用户可加 `--multi-instance` | M0 |
| Q6 | 是否内置示例库 | 建议内置 `sample.sqlite`（只读演示） | M1 |
| Q7 | 自动更新 | v1.0 不做；发布页手动下载 | M9 |

---

## 17. 与 StepDraw 框架的复用清单

| 能力 | 来源 | 用法 |
|---|---|---|
| 窗口/WebView2/DPI | `TauriCPP` | 直接复用；窗口 1120×700，底色 `#FFF7F9` |
| 前端资源内嵌 | `TauriCPP/tools/pack_resources.py` | `EMBED_FRONTEND=ON`；开发模式 `OFF` |
| Bridge invoke/listen | `TauriCPP` | 保留；新增 `ipc.call` 异步层 |
| 文件对话框 | `tauricpp::Dialog` | 打开 SQLite/`.sql`/`.csv`，保存导出 |
| 剪贴板 | `tauricpp::Clipboard` | 可选；前端 `navigator.clipboard` 优先 |
| 拖放文件 | 框架 `file-dropped` 事件 | `.sql`/`.db`/`.csv` 拖放 |
| 关闭回调 | `Window::OnClose` | 未保存确认/取消关闭 |
| 开发模式 | `build.ps1 -DevMode` | UI 改动刷新即可 |
| 单文件部署 | 静态 CRT + 资源内嵌 | 发布 zip 即用 |
| 前端风格 | StepDraw CSS/JS 组织 | 顶栏/按钮/下拉/表单保持一致；配色改为 Silkit 晨樱玫瑰系 token |

**需要对框架做的小改动（尽量少）**
1. `CMakeLists.txt` 拆分出可复用的 `tauricpp` 目标（或在项目顶层复制库目标）。
2. 可选：`Bridge` 增加 `EmitToFrontend` 的批量合并（当前逐条 ExecuteScript，高频事件需在前端节流；v1 先在业务层合并）。
3. 可选：窗口关闭时等待异步任务结束的回调（v1 用应用级 `Shutdown()` 自行 join）。
4. 可选：启动时 WebView2 Runtime 检测与友好提示（若框架未提供）。

---

## 18. 结语

**先做 SQLite 垂直切片，再做 MySQL/PostgreSQL；先保证“连得上、查得快、改得安全”，再补设计与导入导出。**

三个不可妥协的底线：
1. **UI 永不阻塞**——所有 DB IO 走异步 Session 线程。
2. **值永不丢精度**——字符串 + 类型元数据，前端不转 Number。
3. **写操作永远可预览、可回滚（在引擎支持范围内）**——生成 SQL 与执行 SQL 同源。

> 本文随代码演进；任何与实现不一致的地方，以 §0.2 偏差记录为准，并在里程碑结束时回写。

**界面也是产品力**：默认的晨樱配色不是装饰，而是让“每天打开的数据库工具”不冰冷。做 UI 时优先保证数据可读与操作可预期，再谈风格；任何为了好看牺牲对比度或响应速度的改动都应被拒绝。

---

## 19. 功能 Gap 分析：dbx 0.6.69 / GoNavi 1.2.2（2026-11-15）

### 19.1 对标基线与判定口径

| 项目 | 定位 | 技术底座 | 规模证据 |
|---|---|---|---|
| **Silkit v1.0.4** | 单 exe、离线、Windows 轻量数据库工作台 | C++20 + TauriCPP（Win32 + WebView2）+ 原生 JS | 3 引擎、10 个 CTest 目标、约 9MB exe |
| **dbx 0.6.69** | 100+ 数据源的「数据库工作平台」 | Rust/Tauri 2 + Vue 3 + CodeMirror 6 | 27 个 Rust crate、88 个 `dbType`、82 个 Tauri command、299 个 app-tests + 1948 个前端 spec、125 个 Rust 集成测试 |
| **GoNavi 1.2.2** | 桌面优先 + MCP 原生的多数据源工作台 | Go 1.24 + Wails v2 + React/Monaco | 12 内置 + 45+ 可选驱动代理、1097 个 `internal/app` Go 文件、1596 个 Go 测试 + 1355 个前端测试 |

**结论先行**：两者都已超出「轻量 SQL 客户端」，是叠加了 DBA 管理面、数据同步调度、AI/MCP/插件平台与服务端形态的工作平台。Silkit 的差距主要不在「连查改导」主链路（这部分已对齐），而在 **格式广度、可审查/可视化、专业诊断、扩展与部署形态**。逐项照抄会破坏「单 exe / <10MB / 无账号 / 离线」底线，因此按下述口径分级：

| 标记 | 含义 |
|---|---|
| ✅ / ◐ / ✗ | 已具备可用 / 部分或简化 / 缺失 |
| **P0** | v1.1 必补：日常使用会直接碰到，且不改变产品定位 |
| **P1** | v1.2 增强：专业场景，可显著提升完成度 |
| **P2** | v2.0 平台化：需重新评估定位与体积预算 |
| **—** | 明确不做：与轻量/离线/单 exe 冲突，写清理由 |
| **=** | 已对齐，保持现状 |

> 状态口径：Silkit 列以 `README.md` / `CHANGELOG.md` / `src` / `frontend` / 实测为准，规划但未落地的（如 `connections.json` 的 `group`/`color`、`history.jsonl`）记 ◐ 而非 ✅。dbx / GoNavi 列以仓库 README、功能文档与源码模块为准。

### 19.2 功能 Gap 矩阵

#### A. 数据源与驱动

| # | 功能项 | Silkit | dbx | GoNavi | Gap | 建议 / 排期 |
|---|---|---|---|---|---|---|
| A1 | MySQL / MariaDB / PostgreSQL / SQLite | ✅ | ✅ | ✅ | = | 主链路已对齐，继续用真实库回归 |
| A2 | 国产/商业库（达梦、金仓、openGauss、OceanBase、GoldenDB…） | ✗ | ✅ | ✅ | — | 需要各厂商客户端 SDK，违背单 exe 体积与离线分发 |
| A3 | 非关系型（Redis/Mongo/向量/MQ/ES/时序/KV） | ✗ | ✅ | ✅ | — | 不是 SQL 客户端范畴；`DriverRegistry` 仅留接口 |
| A4 | 可选驱动代理（独立进程、按需下载、版本管理） | ✗ | ✅ | ✅ | — | 引入后台进程与更新体系，与轻量定位冲突 |
| A5 | 自定义 JDBC / DSN 驱动扩展 | ✗ | ✅ | ✅ | P2 | 与 v2.0 插件化一并评估 |
| A6 | `DriverRegistry` + `Capabilities` 能力门控 | ◐ 有接口，UI 未按能力隐藏入口 | ✅ Connect/Browse/Understand/Operate 四级 | ✅ 逐库能力矩阵 | P1 | 补齐 Capabilities 字段并让菜单按能力显隐 |
| A7 | 服务端版本 / 会话诊断 / 进程列表 | ◐ 仅版本（本轮修复显示） | ✅ | ✅ | P1 | 先做只读会话列表与锁等待 |
| A8 | 多引擎版本矩阵实测 | ◐ MySQL 8.4.13 / PG 18.7 | ✅ 多版本 | ✅ 契约矩阵 | P1 | MariaDB 11、PG 13–16 未逐版本验证 |

#### B. 连接、安全与凭据

| # | 功能项 | Silkit | dbx | GoNavi | Gap | 建议 / 排期 |
|---|---|---|---|---|---|---|
| B1 | 连接保存 / 测试 / 最近使用 | ✅ | ✅ | ✅ | = | — |
| B2 | 分组 / 颜色 / 备注 / 置顶 / 多选批量 | ◐ 仅 schema 有 `group`/`color`，UI 未接 | ✅ | ✅ | P0 | 补全连接树分组与颜色，成本低、感知强 |
| B3 | 连接配置导入导出（含加密包） | ◐ 仅 settings 备份 | ✅ DBeaver/Navicat 导入 | ✅ Navicat `.ncx` + Excel/JSON | P0 | 先做自有 JSON 导入导出，再考虑 Navicat |
| B4 | SSH 隧道 / SOCKS / HTTP 隧道 | ✗ | ✅ 三类 + Profile | ✅ SSH + 代理 + Navicat 隧道 | P1 | 原 v1.1 项，需引入 libssh，排到 P1 |
| B5 | SSL/TLS 客户端证书 UI | ◐ 基础 `sslmode` | ✅ CA/钱包/Oracle OCI | ✅ CA/证书/SQL Server TLS | P1 | 证书三件套 UI + 连接级隔离 |
| B6 | 断线自动重连 / 连接健康 | ◐ 手工验收有，未自动化 | ✅ | ✅ 健康检查 | P0 | 自动重连 + U-10 E2E |
| B7 | 只读连接 / 临时写解锁 / 生产保护 | ◐ 只读已实现（E2E-10） | ✅ 1/5 分钟临时解锁、生产水印 | ✅ 生产风险确认 | P1 | 临时解锁窗口 + 库级生产标记 |
| B8 | 危险 SQL 分类与二次确认 | ✅ 安全模式 | ✅ 风险分类 + 报告 | ✅ AI 安全三级 + Guard | = | 保持；补充 U-08 自动化 |
| B9 | 凭据加密（DPAPI / keyring / 迁移向导） | ✅ DPAPI | ✅ 平台凭据存储 + 迁移 | ✅ keyring + 每日密钥 | = | 仅 Windows，定位使然 |
| B10 | SQL 审计中心（脱敏、保留、导出、完整性） | ◐ 日志脱敏 | ✅ MCP/CLI 调用历史 | ✅ 审计中心 + WAL 完整性 | P1 | 本地 `audit.jsonl` + 导出，不引服务端 |

#### C. 对象浏览与元数据

| # | 功能项 | Silkit | dbx | GoNavi | Gap | 建议 / 排期 |
|---|---|---|---|---|---|---|
| C1 | 库/模式/表/视图/函数/触发器懒加载树 | ✅ | ✅ | ✅ | = | — |
| C2 | 本地过滤 + 远程 `meta.search` | ◐ 后端已实现，前端仅本地过滤（§0.2） | ✅ 正则/类型/大 schema 索引 | ✅ 元数据发现范围 | P0 | 前端接后端搜索，U-01 验收 |
| C3 | 对象信息面板 / 表概览 / 行数统计 | ◐ 详情面板 + DDL | ✅ Object Browser 三形态 | ✅ 表概览/访问计数 | P1 | 补行数/大小/创建时间统计 |
| C4 | 列、索引、外键、触发器完整元数据 | ✅ 表设计器已用 | ✅ 含 Oracle TYPE/序列/扩展 | ✅ 含唯一键组/字符长度语义 | = | 三引擎范围内已够用 |
| C5 | 数据字典 / 数据库文档 / DBML / PDF | ✗ | ✅ Markdown 笔记 + DBML + 数据字典 PDF | ✗ | P1 | 先做表/列注释导出 Markdown |
| C6 | 字段血缘 / 对象依赖分析 | ✗ | ✅ 置信度分级 | ✗ | P2 | 需要查询历史与视图解析，成本高 |
| C7 | 隐藏系统 schema / 可见范围控制 | ◐ 设置粗略 | ✅ | ✅ 隐藏对象/发现范围 | P1 | 按前缀/系统库白名单过滤 |
| C8 | 视图/函数/触发器定义编辑 | ✗ 只能查看并另存 `.sql` | ✅ 触发器源码编辑 + enable/compile | ✅ 定义查看器 | P1 | 复用 `ddl.preview` 令牌机制（§0.2 已留口） |

#### D. SQL 编辑器与执行

| # | 功能项 | Silkit | dbx | GoNavi | Gap | 建议 / 排期 |
|---|---|---|---|---|---|---|
| D1 | 多标签、语法高亮、元数据补全、执行范围 | ✅ CodeMirror 5；v1.0.2 起补全含表/视图/列与 `表名.` / `别名.`，新建查询继承选中库 | ✅ CodeMirror 6 | ✅ Monaco | = | 已对齐；后续仅补语义级补全（列类型/参数提示） |
| D2 | SQL 格式化 / 折叠 / 片段 / 历史 | ✅ | ✅ 9 种编辑器主题 + Vim 模式 | ✅ Slash 命令 + AI 内联补全 | ◐ P1 | 主题数、Vim、Slash 命令属体验增强 |
| D3 | 查询历史落盘（`history.jsonl`） | ◐ `localStorage` + `settings.json`（§0.2） | ✅ 200–10000 条 | ✅ 来源可切换 MCP | P0 | 落 `history.jsonl` 并轮转 |
| D4 | 多结果集 / 消息 / 取消 / 执行计时 | ✅ | ✅ | ✅ | = | 取消语义由 E2E-09 守护 |
| D5 | DML 只读预览（UPDATE/INSERT/DELETE → SELECT） | ✗ | ✅ | ◐ 变更预览（写侧） | P0 | 保存/执行前先看影响行，契合「显式安全」原则 |
| D6 | 参数/变量（`:name`、`${}`、`@set`） | ✗ | ✅ | ◐ | P2 | 轻量客户端使用频率低 |
| D7 | 多库执行 / 多源结果合并 | ✗ | ✅ | ✅ 批量连接工作台 | P2 | 需会话编排 |
| D8 | SQL 语义诊断 / 风险分类 / 前后端分割一致性 | ◐ 仅执行报错 | ✅ 语义诊断 + `sql_risk` | ✅ SQL 分类器 | P1 | 先保证前端 `SilkitSqlTools` 与后端 `SplitSql` 语义一致（E2E-04/13） |
| D9 | 全局文件内容搜索 / Quick Open | ✗ | ✅ | ✅ | P2 | — |
| D10 | PL/SQL 调试器（断点/调用栈） | ✗ | ✅ Oracle/OceanBase | ✗ | — | 远超三引擎桌面工具范围 |

#### E. 结果网格与数据编辑

| # | 功能项 | Silkit | dbx | GoNavi | Gap | 建议 / 排期 |
|---|---|---|---|---|---|---|
| E1 | 虚拟滚动、分页、服务端筛选/排序 | ✅ | ✅ | ✅ | = | E2E-07/08 守护 |
| E2 | 结果流式背压 `query.ack` | ✗（§0.2 未实现） | ✅ 分批 + 快照 | ✅ 行预算/压缩 | P1 | 超大结果集依赖 `maxRows`，需前端节流配合 |
| E3 | 查询取消 / 语句级进度 | ✅ 取消 | ✅ | ✅ | = | — |
| E4 | 复制为 TSV/CSV/JSON/INSERT/UPDATE | ◐ 计划含 INSERT，UPDATE 未见 | ✅ | ✅ 含列注释/排除主键 | P0 | 补齐 UPDATE 与批量 INSERT 复制 |
| E5 | 变更 SQL 预览 / 反向回滚 SQL / DML 快照 | ◐ 保存预览 | ✅ 预览 | ✅ Change Reverse + DML 快照 | P1 | 可审计变更链 |
| E6 | 批量粘贴、克隆行、Set NULL、默认值 | ◐ 基础 CRUD | ✅ | ✅ | P1 | 网格编辑效率 |
| E7 | 模拟数据生成器 | ✗ | ✅ | ✅ 语义/字典/采样 | P1 | 造测试数据，开发者高频 |
| E8 | 单元格详情：JSON 路径 / HEX / 图片 / 几何 | ◐ 长文本/JSON/BLOB | ✅ 时区/SRID/地图 | ✅ 大字段弹窗编辑 | P1 | 先补 JSON 树与 HEX 视图 |
| E9 | 列格式化器 / 值掩码 / 转置视图 | ✗ | ✅ | ✅ | P2 | — |
| E10 | 外键跳转到引用记录 | ✗ | ✅ | ◐ | P1 | 已有 FK 元数据，实现成本低 |

#### F. 表设计与 DDL

| # | 功能项 | Silkit | dbx | GoNavi | Gap | 建议 / 排期 |
|---|---|---|---|---|---|---|
| F1 | 列/索引/外键/表选项 + SQL 预览 | ✅ 三引擎 | ✅ 五页含触发器/分区 | ✅ 含触发器列表 | ◐ P1 | 补触发器页与分区选项 |
| F2 | CREATE/ALTER/DROP/RENAME/TRUNCATE + 影响行数 | ✅ | ✅ | ✅ | = | `integration_db` DDL 全链路已绿 |
| F3 | 完整 DDL 还原（PG 目录重建偏差） | ◐ `pg_catalog` 简化重建 | ✅ | ✅ `SHOW CREATE` | P0 | README 已知限制；向 `pg_dump --schema-only` 对齐 |
| F4 | 视图/函数/触发器 DDL 保存与编辑 | ◐ 另存 `.sql` | ✅ | ✅ | P1 | 见 C8 |
| F5 | 跨方言 DDL 差异矩阵 / 类型改写 | ◐ `engine_dialect` | ✅ `DdlDialectProfile` + `type_rewrite` | ✅ 迁移内核 | P1 | 为 Schema Diff 铺路 |
| F6 | SQLite ALTER 能力边界提示 | ✅ 明确报错 | ✅ 表重建 | ✅ | = | 保持「不静默降级」 |
| F7 | 源码「Verify without writing」恢复模型 | ✗ | ✅ | ◐ | P2 | — |

#### G. 导入导出、转储与备份

| # | 功能项 | Silkit | dbx | GoNavi | Gap | 建议 / 排期 |
|---|---|---|---|---|---|---|
| G1 | CSV/JSON/SQL/Markdown/HTML 流式导出 | ✅ 五种 | ✅ +XLSX/TSV/INSERT/DBML | ✅ +XLSX/Parquet/NDJSON | **P0** | 补 XLSX（最常被要求），其余按使用频率排 |
| G2 | CSV 导入向导（编码/映射/自动建表/错误行） | ✅ | ✅ +XLSX/Parquet/Navicat `.nb3` | ✅ +XLSX/JSON/SQL.GZ | **P0** | 补 XLSX 导入；E2E-11 已建立回环基线 |
| G3 | 整库/整表 SQL 转储（结构+数据） | ◐ 结构与数据，视图等另存 | ✅ 多库编排 + 分卷 ZIP | ✅ 表/库/视图转储 | P1 | 转储纳入视图/函数/触发器定义 |
| G4 | SQL 文件执行（分块流式、语句级进度/重试） | ◐ 整文件读入（≤512MB） | ✅ 分块 | ✅ `.sql.gz` + GTID/回滚计划 | P1 | README 已知限制，改流式 |
| G5 | 定时逻辑备份 + 保留策略 + 选表恢复 | ✗ | ✅ | ✅ 表级备份 + 云备份 | P2 | 需后台常驻调度，与「关掉即结束」冲突 |
| G6 | Navicat `.nb3` / DBeaver 等第三方格式互操作 | ✗ | ✅ | ✅ `.ncx` | — | 闭源格式，优先级低 |
| G7 | 导出原子提交 / 取消 / 进度 / 历史 | ◐ 流式 + 进度 | ✅ | ✅ 原子导出 + 任务历史 | P1 | 补临时文件原子替换 |
| G8 | 数据字典 PDF / 安全报告 / 影响报告 | ✗ | ✅ | ✅ | P2 | — |
| G9 | 加密配置导出 / 云同步（WebDAV/S3/Gist） | ◐ 本地 `.bak` | ✅ | ✅ WebDAV/S3 | — | 与「无账号、本地优先」冲突 |

#### H. 数据同步 / 迁移 / 对比

| # | 功能项 | Silkit | dbx | GoNavi | Gap | 建议 / 排期 |
|---|---|---|---|---|---|---|
| H1 | Schema Diff（结构对比 + 修复 SQL） | ✗ | ✅ 企业级（重命名检测/影响报告/回滚完整性） | ✅ 结构迁移 + HTML 报告 | P1 | 先做**只读对比**，不做自动部署 |
| H2 | Data Compare（按主键行级比对） | ✗ | ✅ | ✅ 行 diff | P1 | 与 H1 同一工作台 |
| H3 | 数据传输 / 跨库迁移（含类型映射） | ✗ | ✅ | ✅ 跨内核迁移 | P2 | 需任务编排与长事务 |
| H4 | CDC 增量同步 / 水位线 | ✗ | ◐ Dolt 版本 | ✅ MongoDB CDC + watermark | — | 后台服务范畴 |
| H5 | 定时任务调度 + 后台 worker + 开机自启 | ✗ | ✅ 后台备份 | ✅ 三平台自启 | — | 违背单 exe、无常驻进程 |
| H6 | 结果集 Diff / 数据校验向导 | ✗ | ✗ | ✅ | P2 | 低成本可先做两结果集对比 |

#### I. 可视化、诊断与监控

| # | 功能项 | Silkit | dbx | GoNavi | Gap | 建议 / 排期 |
|---|---|---|---|---|---|---|
| I1 | ER 图（外键关系图） | ✗ | ✅ Vue Flow + ELK + SVG/PNG 导出 | ✅ React Flow 节点列级展示 | **P0** | 已有 FK 元数据，做只读轻量版；导出 SVG |
| I2 | 查询结果图表（ECharts） | ✗ | ✅ | ✅ 计划图/Graph 视图 | P1 | 折线/柱状/饼图即可 |
| I3 | 建库草稿直接生成建表 SQL | ✗ | ✅ 图草稿同步数据库 | ✗ | P2 | — |
| I4 | EXPLAIN 计划树 / 热点 / 计划对比 | ◐ 文本 EXPLAIN（E2E-14） | ✅ 树/成本/原始 | ✅ 14+ 方言 + 洞察 | **P0** | 先解析 MySQL/PG 文本计划成树 |
| I5 | 慢查询 / 会话 / 锁等待 / 长事务 / kill | ✗ | ✅ 进程列表 + 会话诊断 | ✅ 会话告警 + kill | P1 | 只读视图优先 |
| I6 | 数据库 Dashboard / 驱动运行时监控 | ✗ | ✅ 多库仪表盘 | ✅ JVM/Nacos/XXL-JOB 面板 | — | 明确定位为「检视工具」，不做长期监控 |
| I7 | 用户/角色/授权管理 | ✗ | ✅ | ✅ 8 类 provider | — | DBA 管理能力，超出目标用户 |
| I8 | 数据生成 / 模型生成 | ✗ | ✅ | ✅ | P1 | 见 E7 |

#### J. AI / MCP / 插件 / 部署形态

| # | 功能项 | Silkit | dbx | GoNavi | Gap | 建议 / 排期 |
|---|---|---|---|---|---|---|
| J1 | AI SQL 助手（多 Provider / Agent / Skills） | ✗ | ✅ 10 类 Provider + CLI Agents + Skills | ✅ 内置 AI + Run Harness + Provider 插件 | P2 | 需联网与密钥管理，与离线定位冲突；若做必须默认不执行 |
| J2 | MCP Server（工具集 / 策略 / 审批 / 审计） | ✗ | ✅ 18+ 工具 + 中央策略 | ✅ stdio/http + SQL 审计 | P2 | 需要对外服务进程，v2.0 再评估 |
| J3 | 插件 SDK / 市场 / 签名沙箱 / Host API | ✗ | ✅ Ed25519 + 沙箱 + Marketplace | ✅ Go/TS SDK + 能力白名单 | — | 引入外部代码执行与包管理，违背单 exe |
| J4 | CLI（headless 查询/导出/批量） | ✗ | ✅ 6 平台二进制 | ✅ 共享数据根 | P2 | 可复用 `src/db`，但要新建入口与发布链 |
| J5 | Web Server / Docker / K8s / 自托管 | ✗ | ✅ Web + 登录验证码 | ✅ admin/viewer + TOTP + Prometheus | — | 与「桌面单机、无账号」定位冲突 |
| J6 | 云同步 / 团队空间 / 配置中心 | ✗ | ✅ WebDAV/Gist | ✅ WebDAV/S3 | — | 同上 |
| J7 | 自动更新 / 代码签名 / 多架构分发 | ✗ 便携 zip | ✅ | ✅ 安全更新引擎 + 三源下载 | P1 | 至少做版本检查与手动更新引导 |
| J8 | 深链接 / 单实例 / 分离窗口 / 多显示器 | ◐ 单实例 | ✅ | ✅ 原生分离窗口 | P2 | — |
| J9 | 主题：5 套 + 明暗 | ✅ | ✅ 深色 + 9 编辑器主题 | ✅ 多套 + 自定义主题 + 背景/字体画廊 | ◐ P1 | 补自定义主题，画廊不做 |
| J10 | 国际化：中/英（动态视图未覆盖） | ◐ 2 语言，部分中文硬编码 | ✅ 14 语言 | ✅ 6 语言 + CI 扫描门禁 | **P0** | 补齐对象树/历史/设计器/导入导出文案 + 缺 key 检查 |

#### K. 工程与测试体系

| # | 功能项 | Silkit | dbx | GoNavi | Gap | 建议 / 排期 |
|---|---|---|---|---|---|---|
| K1 | 单元测试规模 | 10 个 CTest 目标 | 299 app-tests + 1948 spec + 125 Rust 集成 | 1596 Go 测试 + 1355 前端测试 | P1 | 不追数量，按「纯逻辑 + 真库」双轨补 |
| K2 | 真实库集成测试（live / 契约矩阵） | ✅ `integration_db` + `e2e_real_db`（本轮新增） | ✅ 60+ `live_*.rs` + Docker 矩阵 | ✅ `*_live_test.go` + `tools/contract-test` | = | 已补齐双引擎真实库基准 |
| K3 | 断言式 UI E2E（Playwright/Cypress） | ✗（仅 §12.5 手工矩阵） | ◐ 仅 UI 预览录制（非断言） | ✅ Vitest 集成/组件测试 | **P0** | §12.4.2 的 U-01~U-12 待接入 |
| K4 | CI 门禁（构建/测试/对比度/i18n/架构契约） | ◐ CTest + 对比度 125 项 | ✅ 1737 行 ci.yml + 33 工作流 | ✅ 9 工作流 + i18n 扫描 + actionlint | P1 | 接入 E2E runner 与 i18n 缺 key 检查 |
| K5 | 性能基准与内存/DOM 审计 | ◐ §12.6 预算，未实测 | ✅ 大 schema / DOM / 内存审计 | ✅ 请求追踪 / IPC profile | P1 | 把 §12.6 预算变成可跑的基准 |
| K6 | 崩溃 minidump / 复现包 / 请求诊断 | ◐ 文本崩溃日志 | ✅ 请求诊断 | ✅ 脱敏复现包 | P2 | 可选 minidump |
| K7 | 文档：功能文档站 / 变更日志 / 路线图 | ◐ README + CODEPLAN + CHANGELOG | ✅ 70+ 篇 docs | ✅ 专题文档 + Roadmap | P1 | 按功能补「使用说明」而非堆文档 |
| K8 | 版本兼容（Win7 / 多架构 / 跨平台） | ✗ Windows + WebView2 | ✅ Win7 兼容构建 | ✅ 三平台 | — | TauriCPP 是 Win32 壳，跨平台需另做 |

### 19.3 优先级结论

**P0（v1.1，完成度底线，不改变定位）**
1. XLSX 导入/导出（G1/G2）——对标中最普遍的格式缺口。
2. 结果复制为 INSERT/UPDATE、DML 只读预览（E4/D5）——延续「写操作可预览」底线。
3. ER 只读关系图（I1）与 EXPLAIN 计划树（I4）——已有元数据/计划数据源，投入产出比高。
4. 连接分组/颜色落地、配置导入导出、断线自动重连（B2/B3/B6）。
5. 对象树远程搜索接前端（C2）、查询历史落 `history.jsonl`（D3）、PG 完整 DDL 还原（F3）。
6. i18n 补齐动态视图（J10）、前端 Playwright E2E 与 CI 接入（K3/K4）。

**P1（v1.2，专业增强）**：SSH/代理、SSL 证书 UI、只读 Schema/Data Diff、结果图表、单元格详情增强、模拟数据、会话/锁等待面板、SQL 审计、流式 SQL 文件执行、转储纳入视图定义、`query.ack` 背压、触发器/分区设计器、`Capabilities` 门控、多引擎版本矩阵、版本检查。

**P2（v2.0，需重新评估定位）**：新驱动（SQL Server/Oracle）、数据传输同步、定时备份、插件 SDK、MCP Server、AI 助手（显式授权、默认不执行）、CLI、Vim/Slash 命令、字段血缘、结果集 Diff、minidump。

**明确不做（写清理由）**：100+ 异构数据源与可选驱动代理（需厂商 SDK/后台进程）、Web/Docker/K8s/CLI 服务端形态（需账号体系与网络暴露）、云同步与团队空间（违背本地优先）、插件市场与签名沙箱（引入外部代码执行）、后台常驻调度与 CDC（违背「关掉即结束」）、用户权限管理与长期监控（目标用户不是 DBA）、PL/SQL 调试器（超出三引擎范围）、Win7/跨平台（TauriCPP 是 Win32 + WebView2 壳）。

### 19.4 Gap → E2E 映射

| 已有 E2E 守护 | 覆盖的 Gap / 风险 |
|---|---|
| E2E-01/02/03/04/13 | A6 能力位、A7 版本号、B3/B6 连接错误、D8 分割一致性 |
| E2E-05/06/12 | C1/C4 元数据、数值精度回归、真实数据正确性、PG 空表列头 |
| E2E-07/08/09 | E1 虚拟网格后端、E2 背压前置、E3 取消、§12.6 性能预算 |
| E2E-10/11/14 | B7/B8 只读与安全、G1/G2 格式基线、I4 计划数据源 |

**新增功能必须同步补的 E2E 项**（沿用 `E2E-xx` 编号，双引擎各跑一遍）：

| 拟编号 | 触发功能 | 断言要点 |
|---|---|---|
| E2E-15 | XLSX 导入/导出（P0） | 中文/数字/日期/NULL 往返，>1,048,575 行分 sheet |
| E2E-16 | 复制为 INSERT/UPDATE（P0） | 生成 SQL 可直接在三引擎执行且值不丢精度 |
| E2E-17 | DML 只读预览（P0） | 预览不产生副作用，影响行数与执行一致 |
| E2E-18 | ER 图 / EXPLAIN 树（P0） | FK 边数与 `information_schema` 一致；计划节点非空 |
| E2E-19 | 断线重连 / 配置导入导出（P0） | 重连后标签保留；配置 roundtrip 密码不出进程 |
| E2E-20 | PG 完整 DDL（P0） | 重建 SQL 与 `pg_dump --schema-only` 关键对象一致 |
| E2E-21 | Schema Diff / Data Compare（P1） | 只读对比不写库；Added/Removed/Modified 计数正确 |
| E2E-22 | 审计 / i18n / 主题（P0-P1） | 日志不含密码；动态视图无硬编码中文；对比度 ≥4.5 |

### 19.5 本轮实测发现与修复（2026-11-15）

| # | 发现 | 处理 |
|---|---|---|
| 1 | PG 对 0 行 `SELECT` 不回传列元数据（E2E-05 首轮 FAIL） | 修 `src/db/drivers/postgres_driver.cpp`：抽出 `capture_columns()`，`PGRES_TUPLES_OK` 也采集列头并补发空批次；双引擎转绿 |
| 2 | PG 服务端版本显示为 `18.0.7`（实际 18.7） | 同文件 `ServerVersion()` 对 `PQserverVersion >= 100000` 使用 `major.minor`；实测 `18.7` |
| 3 | 系统 PG 无 `employees` 库，与口述测试源不符 | 新增 `tests/e2e/seed_pg_employees.sql` 精简镜像 + runner `--seed-pg-employees`；自动探测/回退并告警 |
| 4 | 大小写重复的代理变量触发 MSBuild `MSB6001`，CL.exe 无法启动 | `tools/e2e_runner.py` 构建子进程剔除小写重复键；§12.1 记录手工等价命令 |
| 5 | §0.1 里程碑状态、§12.1 环境、§12.2 测试清单、§12.4 E2E 均与实现不符 | 全文回写，见 §0.2 新增偏差记录 |

**验证结果**：`ctest -C Release` 10/10 通过；`e2e_real_db` 28 passed / 0 failed / 0 skipped（MySQL 8.4.13 `employees` + PostgreSQL 18.7 `employees`）；`integration_db` 双引擎 M5 CRUD / M6 DDL 写路径全绿；报告 `build/e2e-report.md`。

**v1.0.2 回归（2026-11-15，含空态与补全修复）**：`ctest -C Release` 10/10 通过；`e2e_real_db` 31 PASS / 0 FAIL（14 项 × MySQL 8.4.13 + PostgreSQL 18.7，另加 3307 无 TLS `SSL-01~03`）；`integration_db` 双引擎 M5/M6 写路径全绿；`node tools/test_hint.js` 31 项、`node tools/test_tabs.js` 5 项全通过；全部前端 JS `node --check` 通过；`tools/check_version.py` 8 处版本一致（1.0.2）；报告 `build/e2e-report.md`。

**v1.0.3 回归（2026-11-15，补全修复）**：`ctest -C Release` 10/10 通过；`node tools/test_hint.js` 45 项、`node tools/test_tabs.js` 5 项全通过；全部前端 JS `node --check` 通过；`tools/check_version.py` 8 处版本一致（1.0.3）；Release exe 重新嵌入前端资源并重建便携包。

**v1.0.4 回归（2026-11-15，展示优化）**：`node tools/test_hint.js` 46 项、`node tools/test_tabs.js` 5 项全通过；全部前端 JS `node --check` 通过；`ctest -C Release` 10/10；`tools/check_version.py` 8 处版本一致（1.0.4）；Release exe 重新嵌入前端资源并重建便携包。

### 19.6 证据索引

| 用途 | 路径 |
|---|---|
| dbx 功能与文档 | `D:\Cplusplus\dbx-0.6.69\README.md` / `README.zh-CN.md`、`docs/content/docs/`（databases / schema-browser / query-editor / data-grid / table-structure / table-import / database-export / schema-diff / data-transfer / database-backup / ai-assistant / mcp / plugin-development / production-safety / ssh-tunnel / specialized-workspaces）、`handoff.md`、`crates/dbx-core/assets/database-drivers.manifest.json`、`plugins/`、`packages/app-tests/`、`crates/dbx-core/tests/` |
| GoNavi 功能与文档 | `D:\Cplusplus\GoNavi-1.2.2\README.md` / `README.zh-CN.md`、`AGENTS.md`、`AI_EXTENSIONS_ROADMAP.md`、`DATA_SYNC_TASKS.md`、`HIVE_SUPPORT.md`、`cmd/gonavi-mcp-server/README.md`、`internal/{app,db,ai,mcpserver,sync,plugin,dbuser,sqlaudit,webserver}/`、`frontend/src/components/`、`tools/contract-test/README.md`、`.github/workflows/backend-tests.yml` |
| Silkit 本轮产物 | `CODEPLAN.md` §0/§1.3/§4/§12/§14/§19、`tests/e2e/e2e_real_db.cpp`、`tests/e2e/seed_pg_employees.sql`、`tests/CMakeLists.txt`、`tools/e2e_runner.py`、`src/db/drivers/postgres_driver.cpp`、`build/e2e-report.md` |

> 本章是「唯一计划源」的增量：任何与实现不一致的地方仍以 §0.2 偏差记录为准；Gap 优先级调整时同步刷新 §14 路线图与 §12.4 测试矩阵。

