# 更新日志

本项目遵循里程碑版本号；日期为交付日期。

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
