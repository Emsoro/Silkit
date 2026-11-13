# Silkit

跨库数据库工作台（MySQL / MariaDB / PostgreSQL / SQLite），Windows 桌面端。
C++17 + Win32 + WebView2，前端为原生 HTML/CSS/JS，无 Node 构建步骤。

> 当前进度：**M0 — 工程骨架、异步 IPC、UI 外壳、五套主题**（v0.0.1）。
> 数据库连接与查询执行从 M1 开始接入，详见 [CODEPLAN.md](CODEPLAN.md)。

## M0 已交付

- 可构建的单文件 `Silkit.exe`（静态 CRT，仅依赖系统 WebView2 运行时）
- 异步 IPC：`ipc.call` 立即返回，worker 线程池通过 `ipc.event` 回传 `progress / result / error / cancelled`
- UI 外壳：顶栏、对象树、SQL 编辑器、结果网格、消息面板、状态栏、分栏拖拽
- 五套主题 + 顶栏切换器 + 快捷键，主题与窗口底色持久化，启动无白闪
- 配置写入 `%APPDATA%\Silkit\settings.json`（原子替换），日志按天写入 `%APPDATA%\Silkit\logs\`
- 自检入口：顶栏「自检」跑一次 3 步异步任务，可在执行中取消
- `tools/contrast_check.py`：校验五套主题的关键配色对比度（WCAG 2.1）

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
C:\vcpkg\vcpkg.exe install webview2:x64-windows-static --classic
```

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
│  ├─ core/                    # 路径、日志、设置、JSON 合并、profiles 占位
│  └─ ipc/                     # AsyncRouter（worker 池）+ M0 命令
├─ frontend/
│  ├─ index.html               # 外壳 + 首帧防闪脚本
│  ├─ css/                     # tokens / base / layout + themes/*.css
│  └─ js/                      # bridge / api / themes / app / icons / mock
├─ tests/                      # CTest 冒烟测试（JSON 合并、默认配置）
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

## 已知限制（M0）

- SQL 编辑器是带行号的 `textarea`，CodeMirror 在 M4 接入
- 对象树为静态演示数据，M1 接入真实元数据
- 仅 `app.*` / `settings.*` / `theme.*` 命令；连接、查询、导出尚未实现
- DevMode 下需把 `frontend/` 放在 exe 同级目录（或 `build\Release\..\..\frontend`）

## 许可

见 [LICENSE](LICENSE)。内置 TauriCPP 框架版权归其原作者所有。
