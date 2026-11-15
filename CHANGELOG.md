# 更新日志

本项目遵循里程碑版本号；日期为交付日期。

## [1.0.4] — 2026-11-15

### 变更

- **补全关键字去标签**：移除候选右侧的 `keyword` 标签。常用/冷门两档关键字都不再携带 `_source` / `_badge`，`renderHintItem()` 对关键字只渲染左侧名称、不生成 `.cm-hint-meta`；列（`库.表`）与表（库/schema 名）的来源路径保持不变，纯文本降级 `displayText` 也同步去掉关键字后缀。
- **自测**：`tools/test_hint.js` 45 → **46 项**，把「右侧显示 keyword」断言反向更新为「关键字无右侧标签」，并新增「关键字 render 不生成右侧标签」断言；`tools/test_tabs.js` 5 项保持通过。
- **版本号**：`1.0.3` → **`1.0.4`**（patch：展示细节调整，不占用 `v1.1`）。CMake、`vcpkg.json`、`src/app/version.h`、状态栏、Mock 后端、中英文 README 版本说明 / 发布包名 / 验收清单 / 已知限制标题、`CODEPLAN.md` 同步；`tools/check_version.py` 校验 8 处全绿。

## [1.0.3] — 2026-11-15

### 修复

- **补全看不到 SELECT 列表里的列**：此前只扫描光标之前的文本，写 `SELECT departments.dept_no, d| FROM departments` 这类 SELECT 列表在前的 SQL 时，`FROM` 尚未进入解析范围，输入 `d` 不会提示 `dept_name`。现在以分号切分光标所在的整条语句（含光标之后的部分）解析 `FROM / JOIN / UPDATE / INTO`，并剥离 `--` / `/* */` 注释；列候选限定于语句中真实出现的表，语句已写出 `FROM` 表时不再混入对象树里选中的其它表，前后语句之间也不会串列。
- **候选展示杂乱**：名称后面不再跟 `(table)` / `(view)` 后缀。

### 新增

- **右侧所属路径**：借助 CodeMirror `show-hint` 的自定义 `render`，候选左侧保持名称，右侧统一右对齐显示类型与来源——列 `employees.departments`（PG 为 `schema.表`）、表 `employees`、关键字 `keyword`（该标签已在 v1.0.4 移除）；跨库限定符会随目标库变化（如 `sakila.actor`）。样式复用五套主题的 `--text-dim` / `--text-faint` / `--accent-hover`，无 `render` 的旧版降级仍保留纯文本信息。

### 变更

- **自测**：`tools/test_hint.js` 由 31 项扩充到 **45 项**，新增「FROM 位于光标之后」「带别名」「多语句隔离」「注释不串列」「来源路径 / render」等回归；`tools/test_tabs.js` 5 项保持通过。
- **版本号**：`1.0.2` → **`1.0.3`**（patch：补全缺陷修复与展示优化，不占用 `v1.1`）。CMake、`vcpkg.json`、`src/app/version.h`、状态栏、Mock 后端、中英文 README 版本说明 / 发布包名 / 验收清单 / 已知限制标题、`CODEPLAN.md` 同步；`tools/check_version.py` 校验 8 处全绿。

## [1.0.2] — 2026-11-15

### 新增

- **新建查询自动 USE**：对象树选中数据库 / 表 / 视图后，顶栏「新建查询」、标签栏 `+`、`Ctrl+N` 创建的新标签继承该连接与库；与会话当前库不一致时自动执行 `USE`（MySQL）/ `conn.set_database`（PG 重连、SQLite 仅已 ATTACH 库），编辑器标题显示当前库。
- **查询页连接 / 库下拉**：编辑器工具条新增两个下拉，改连接或改库立即生效；切换标签或执行 SQL 时若会话被别的标签切走，会自动切回本标签记录的库。标签的 `database / schema / table` 随会话恢复一起持久化；切库后清空该会话补全缓存并刷新对象树。
- **智能补全**：候选从「关键字 + 元数据表」升级为关键字 + 当前库表/视图 + 列；支持 `表名.` / `别名.` 只提示该表列（带类型说明、插在点号后）、`FROM / JOIN / UPDATE / INTO` 表的列进入裸标识符候选、PG schema 透传。元数据按 `会话 | 引擎 | 库 | schema` 缓存，异步按需拉取，DDL 变更与切库自动失效。
- **自测**：新增 `tools/test_hint.js`（`node tools/test_hint.js`，无浏览器，31 项），覆盖表/视图、点号列补全、别名、库隔离、缓存失效与 schema 透传；新增 `tools/test_tabs.js`（`node tools/test_tabs.js`，5 项）覆盖标签空态回归。

### 修复

- **查询页可彻底关闭**：此前 `tabs.init()` 在启动时无标签就自动建一个查询页、`closeTab()` 关闭最后一个标签后又补建一个、`onOpen()` 连接成功时还会再建一个，导致「一打开软件就有查询页，一点关就又弹出来一个」。现在启动、连接成功、关闭最后一个标签都停在「无标签」空态卡片（新建查询 / 打开 SQL / `Ctrl+N` 引导），编辑器、结果区与分割条一并隐藏；空标签集随会话恢复持久化，重启后仍保持关闭。
- **关闭非激活标签不误切换**：关闭后台标签时改用当前激活标签而非按位置取相邻标签，避免主区域意外跳到别的标签。
- **空态 i18n**：新增中英文文案键（`empty.*`），五套主题下复用 `--bg-*` / `--accent-soft` 变量，不引入硬编码颜色。
- **点号补全串列**：输入 `employees.` 时曾可能弹出别的表的列。原因是 `resolveTable` 在解析不到表时直接把限定符当表名去查列，且 `tableRefs` 会把 `FROM employees.birth_date` 里的列名误当成表，裸标识符分支又把这些伪表的列一起塞进候选。现在：`表名.` / `别名.` 只返回已确认存在的表的列，解析不到就返回空，不再混入关键字、表名或其它表的列；`FROM / JOIN / UPDATE / INTO` 后的 `库名.`（PG 为 `schema.`）识别为库限定符，提示该库的表名；裸标识符只补当前库里确实存在的表的列。补全自测扩充到 31 项。

### 变更

- **补全候选排序**：按「常用 SQL 关键字 → 表名 → 列名 → 冷门关键字」分组排序。常用项（`SELECT/FROM/WHERE/JOIN/ORDER BY/...`）占首屏，函数、`PRIMARY KEY`、`AUTO_INCREMENT`、`SHOW ...`、引擎专属子句等冷门项保留但排到末尾，不再挤掉表名/列名。`tools/test_hint.js` 扩充到 31 项。
- **版本号**：`1.0.1` → **`1.0.2`**（patch：体验增强，不占用已排给 SSH / ER 图的 `v1.1`）。CMake、`vcpkg.json`、`src/app/version.h`、状态栏、Mock 后端、中英文 README 版本说明 / 发布包名 / 验收清单、`CODEPLAN.md` 同步；`tools/check_version.py` 校验 8 处全绿。
- **补全触发**：输入字母 / 点号的延迟由 500ms 降到 220ms；`Ctrl+Space` 与自动触发在无连接时不再拉起 anyword 兜底。

## [1.0.1] — 2026-11-15

### 修复

- **确认弹窗主题化**：连接列表 / 对象树右键「删除连接」等 13 处使用 `window.confirm`，在 WebView2 里是系统原生白底弹窗，不跟随明暗主题。新增应用内通用确认框（`#appConfirm` + `U.confirm()`：标题 / 正文 / 说明 / SQL 预览、危险按钮配色、Esc / Enter / 遮罩关闭），全部调用点改为 `await`；确认框打开时全局快捷键不再穿透。
- **MySQL 连接 SSL**：MariaDB Connector/C 3.4.5 默认强制校验证书，导致 `disable` 也无法连接不声明 `CLIENT_SSL` 的 MySQL 协议服务端（如 SQLTiny），报 `2026 SSL is required, but the server does not support it`。现在 `prefer` 自动降级明文、`disable` 始终明文、`require`/`verify-*` 保持严格；2026 错误附带可操作提示。新增可选 E2E `SSL-01~03`。
- **复制提示**：`U.copyText` 在 WebView2 的 `execCommand` 回退路径上改为始终返回 Promise，复制失败可被 `.catch()` 捕获。

### 新增

- **连接失败诊断**：连接对话框错误条与错误 toast 最右侧新增「复制信息」按钮，复制含错误码、SQLSTATE、提示与连接上下文（不含密码）的完整诊断；错误条同时展示此前被忽略的 `hint`。
- **英文文档**：新增 `README.en.md`（与中文 README 章节一一对应），两份 README 顶部互加语言切换链接；`tools/package.ps1` 将英文版一并打进便携发布包。

### 变更

- **编译标准**：`CMAKE_CXX_STANDARD` 由 17 升级为 **20**（`REQUIRED ON` / `EXTENSIONS OFF` 不变），需 VS 2022（`/std:c++20`）；README 与 `CODEPLAN.md` 中的技术栈、环境要求、CMake 片段与对标表同步更新。TauriCPP 内置副本由顶层统一按 C++20 编译，其上游自述文件保持原样。
- **版本号**：`1.0.0` → **`1.0.1`**（patch：缺陷修复与小功能；`v1.1` 仍留给 SSH / ER 图等里程碑）。CMake、`vcpkg.json`、`src/app/version.h`、状态栏、Mock 后端、README 版本说明与便携包文件名同步，`tools/check_version.py` 全绿。
- **界面精简**：移除顶栏 `M8` 徽章与状态栏「M8 · 等待连接 / 已连接」（里程碑编号对使用者无意义）；右下角版本号沿用原徽章的小胶囊样式，并清理对应的 i18n 文案与赋值逻辑。

## [1.0.0] — 2026-11-14

首个正式版：Windows 单文件数据库工作台，内置 MySQL / MariaDB / PostgreSQL / SQLite 驱动。

### 新增

- **发布**：版本号统一为 1.0.0，`tools/check_version.py` 校验一致性；`tools/package.ps1` 生成 `dist/Silkit-1.0.0-win-x64.zip` 便携包。
- **应用信息**：exe 内嵌多分辨率图标（标题栏/任务栏），窗口标题「Silkit — 跨库数据库工作台」，关于页展示版本与数据/日志路径。
- **许可**：新增 `THIRD-PARTY-NOTICES.md`，列明 TauriCPP、nlohmann/json、WebView2 SDK、SQLite、libmariadb、libpq、CodeMirror 的许可证。

### 里程碑回顾

- **M0 工程骨架**：TauriCPP（C++17 + Win32 + WebView2）壳、异步 IPC、五套主题、防白闪、日志与原子配置。
- **M1 SQLite**：连接 → 对象树 → 查询 → 流式结果链路，会话线程模型与取消。
- **M2 / M3 MySQL / MariaDB、PostgreSQL**：流式读取、参数化、取消、元数据与 DDL、错误映射。
- **M4 查询工作区**：CodeMirror 多标签、补全、多结果集、EXPLAIN、历史/片段、`.sql` 读写与格式化。
- **M5 表格数据编辑**：虚拟网格、分页筛选、内联增删改、事务保存/回滚、单元格查看。
- **M6 表设计器**：三引擎 DDL 生成、SQL 预览与令牌执行、影响行数与风险提示。
- **M7 导入导出**：CSV 导入向导、五种格式流式导出、SQL 转储、SQL 文件执行、编码转换。
- **M8 设置与安全**：设置面板、配置备份恢复、安全模式危险语句确认、日志脱敏、崩溃日志、会话恢复、中英文界面。
- **M9 发布 1.0**：图标/版本/关于页、支持矩阵与发布清单、第三方许可、便携发布包。

### 已知限制

详见 [README.md](README.md#已知限制v101)：暂不支持 SSH 隧道、XLSX、ER 图与跨平台；PG 表 DDL 为目录重建；200% DPI 与干净虚拟机冒烟待人工留档。

## [0.8.0] — 2026-11-14

设置面板、安全模式、日志脱敏、会话恢复与中英界面（M8）。

## [0.7.0] — 2026-11-14

CSV 导入、流式导出、SQL 转储与 SQL 文件执行（M7）。

## [0.6.0] — 2026-11-14

表设计器与三引擎 DDL 管理（M6）。

## [0.5.0] — 2026-11-14

表格数据编辑：CRUD、筛选、分页与单元格查看（M5）。

## [0.4.0] — 2026-11-14

查询工作区：编辑器、多结果、历史与补全（M4）。

## [0.3.0] — 2026-11-14

PostgreSQL 驱动（M3）。

## [0.2.0] — 2026-11-14

MySQL / MariaDB 驱动（M2）。

## [0.1.0] — 2026-11-13

SQLite 垂直切片与工作台外壳（M1）。

## [0.0.1] — 2026-11-13

工程骨架、异步 IPC、UI 外壳与主题切换（M0）。
