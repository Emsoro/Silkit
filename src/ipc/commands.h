#pragma once

#include "core/profiles.h"
#include "core/settings.h"
#include "db/session_manager.h"
#include "ipc/async_router.h"

namespace tauricpp {
class Window;
}

namespace silkit::ipc {

/// 注册全部 IPC 命令：app.*、settings.*、theme.*、conn.*、meta.*、query.*。
void RegisterCommands(AsyncRouter& router,
                      tauricpp::Window& window,
                      Settings& settings,
                      ProfileStore& profiles,
                      session::SessionManager& sessions);

/// 主题 id → 窗口底色（#RRGGBB）；未知 id 回退到 bloom。
std::string ThemeBackground(const std::string& theme_id);

/// 把窗口类背景刷成主题底色，避免启动/缩放瞬间闪白。
void SetWindowBackground(tauricpp::Window& window, const std::string& theme_id);

}  // namespace silkit::ipc
