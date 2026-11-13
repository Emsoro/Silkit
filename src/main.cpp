// Silkit —— 跨库数据库工作台（MySQL / PostgreSQL / SQLite）
//
// M1：SQLite 连接 → 对象树 → 查询 → 流式结果的端到端工作台。
// UI 全部在前端；C++ 负责窗口、异步任务路由、数据库会话、配置与日志。
#include "app/version.h"
#include "core/logger.h"
#include "core/paths.h"
#include "core/profiles.h"
#include "core/settings.h"
#include "db/session_manager.h"
#include "ipc/async_router.h"
#include "ipc/commands.h"

#include <tauricpp/app.hpp>

#include <Windows.h>

#include <string>

namespace {

COLORREF ParseHexColor(const std::string& hex, COLORREF fallback) {
    if (hex.size() != 7 || hex[0] != '#') return fallback;
    auto channel = [&hex](size_t offset) -> BYTE {
        return static_cast<BYTE>(std::stoi(hex.substr(offset, 2), nullptr, 16));
    };
    return RGB(channel(1), channel(3), channel(5));
}

/// 第二次启动时激活已有窗口（标题即窗口标识）。
void ActivateExistingInstance() {
    if (HWND existing = FindWindowW(nullptr, L"Silkit")) {
        if (IsIconic(existing)) {
            ShowWindow(existing, SW_RESTORE);
        }
        SetForegroundWindow(existing);
        FLASHWINFO info{};
        info.cbSize = sizeof(info);
        info.hwnd = existing;
        info.dwFlags = FLASHW_TRAY;
        info.uCount = 2;
        FlashWindowEx(&info);
    }
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    HANDLE instance_mutex = CreateMutexW(nullptr, FALSE, L"Local\\Silkit.SingleInstance");
    if (instance_mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        ActivateExistingInstance();
        CloseHandle(instance_mutex);
        return 0;
    }

    if (!silkit::Paths::Initialize()) {
        MessageBoxW(nullptr, L"无法创建数据目录 %APPDATA%\\Silkit，请检查权限。", L"Silkit",
                    MB_OK | MB_ICONERROR);
        if (instance_mutex) CloseHandle(instance_mutex);
        return 1;
    }

    silkit::Logger::Instance().Init(silkit::Paths::LogsDir());
    silkit::LogInfo(std::string("Silkit ") + silkit::kVersion + " 启动");

    silkit::Settings settings;
    settings.Load(silkit::Paths::SettingsFile());

    silkit::ProfileStore profiles(silkit::Paths::ProfilesFile());
    profiles.Load();
    silkit::session::SessionManager sessions;

    const auto snapshot = settings.Snapshot();
    const std::string theme = snapshot["ui"].value("theme", std::string{"bloom"});
    const std::string bg_hex = silkit::ipc::ThemeBackground(theme);

    tauricpp::App::Config config;
    config.window_config.title = "Silkit";
    config.window_config.width = 1120;
    config.window_config.height = 700;
    config.window_config.center = true;
    config.window_config.resizable = true;
    config.window_config.devtools = true;
    config.window_config.bg_color = ParseHexColor(bg_hex, RGB(255, 247, 249));

    tauricpp::App app(config);
    silkit::ipc::AsyncRouter router(app.GetBridge(), 4);

    // 窗口在 App::Run() 内部创建，因此命令注册必须放进 OnSetup。
    // 窗口类背景与 WebView2 DefaultBackgroundColor 已在 Window::CreateNativeWindow /
    // SetWebViewBackgroundColor 中按 config.bg_color 设置，启动阶段不会闪白。
    app.OnSetup([&router, &settings, &profiles, &sessions](tauricpp::App& instance) {
        silkit::ipc::RegisterCommands(router, instance.GetWindow(), settings, profiles, sessions);
    });

    silkit::LogInfo("Silkit 窗口已创建，主题=" + theme);
    const int rc = app.Run();

    router.Shutdown();
    sessions.Shutdown();
    settings.Flush();
    silkit::LogInfo("Silkit 退出");
    silkit::Logger::Instance().Shutdown();

    if (instance_mutex) CloseHandle(instance_mutex);
    return rc;
}
