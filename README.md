# Silkit

跨库数据库工作台（MySQL / MariaDB / PostgreSQL / SQLite），Windows 桌面端。
C++17 + Win32 + WebView2，前端为原生 HTML/CSS/JS，无 Node 构建步骤。

> 当前进度：**M1 SQLite 切片 + M2/M3 三引擎驱动（已对照真实服务器验收）+ M4 查询工作区已交付**：连接 → 对象树 → CodeMirror SQL 编辑器（补全/格式化/执行范围）→ 流式结果（多结果子标签/CSV/JSON 导出/EXPLAIN）→ 历史/片段 → 标签持久化恢复。
> 完整计划与偏差记录见 [CODEPLAN.md](CODEPLAN.md)。

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
- **测试**：`engine_dialect`（类型/错误/SQL 生成，无需服务器）、`integration_db`（按 `SILKIT_MYSQL_*` / `SILKIT_PG_*` 环境变量连接真实服务器，未设置时跳过）。

> 验收记录：MySQL 8.4.13 与 PostgreSQL 18.0.7 均完成连接、`SELECT 1`、对象树（库/文件夹/表/列/详情/搜索/DDL）、取消与错误提示的端到端验证（含本机部署的 MySQL 8.4.13 与 PostgreSQL 18）。
> 已知缺口：PG 表 DDL 为目录简化重建，与 `pg_dump --schema-only` 有差距；MariaDB 11、PostgreSQL 13–16 未逐版本部署（同驱动路径）；类型矩阵与性能验收（5000 表 / 1000 表元数据）未实测。

## 构建

### 环境要求

| 依赖 | 版本 / 说明 |
|---|---|
| Windows | 10 1809+ / 11 |
| Visual Studio | 2022（Desktop development with C++） |
| CMake | 3.21+ |
| vcpkg | 安装在 `C:\vcpkg`，或设置 `VCPKG_ROOT` |
| Python | 3.8+（前端资源打包、对比度检查） |
| WebView2 Runtime | Win11 自带；Win10 需安装 Evergreen Runtime |

准备 vcpkg 依赖（只需一次）：

```powershell
C:\vcpkg\vcpkg.exe install webview2:x64-windows-static sqlite3:x64-windows-static `
    libmariadb:x64-windows-static libpq:x64-windows-static --classic
```

> `vcpkg.json` 的 `builtin-baseline` 已固定到当前 vcpkg 仓库中存在的提交（`ef7dbf94…`），`build.ps1` 的 manifest 模式可直接解析全部依赖。若本机 vcpkg 为浅克隆且仍提示找不到基线，可先用上面的 classic 模式预装依赖，再以 `-DVCPKG_MANIFEST_MODE=OFF` 配置 CMake。

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

### 对比度检查

```powershell
python tools\contrast_check.py      # 只显示失败项与汇总
python tools\contrast_check.py -v   # 显示全部 125 项配对
```

## 快捷键

| 快捷键 | 功能 |
|---|---|
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

## 已知限制（v0.1.0）

- 仅 SQLite 驱动可用；MySQL / MariaDB（M2）、PostgreSQL（M3）尚未接入
- SQL 编辑器仍是带行号的 `textarea`；CodeMirror、自动补全、SQL 文件与片段在 M4 接入
- 对象树搜索为已加载节点的本地过滤；后端 `meta.search` 已就绪，前端结果渲染并入 M4
- 表数据只读（结果网格可复制/查看）；CRUD、筛选、分页、表设计器在 M5 / M6
- 导入 / 导出 / 转储、结果导出向导在 M7
- 查询历史暂由前端 `localStorage` + `settings.json` 持久化，M4 迁移到后端 `history.jsonl`
- `query.ack` 背压、`ui_state.*` 会话恢复尚未实现
- DevMode 下需把 `frontend/` 放在 exe 同级目录（或 `build\Release\..\..\frontend`）

## 许可

见 [LICENSE](LICENSE)。内置 TauriCPP 框架版权归其原作者所有。
