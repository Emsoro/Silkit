#pragma once

#include "core/settings.h"
#include "ipc/async_router.h"

namespace tauricpp {
class Window;
}

namespace silkit::ipc {

/// 注册 M0 阶段的全部 IPC 命令：app.*、settings.*、theme.*。
void RegisterCommands(AsyncRouter& router, tauricpp::Window& window, Settings& settings);

/// 主题 id → 窗口底色（#RRGGBB）；未知 id 回退到 bloom。
std::string ThemeBackground(const std::string& theme_id);

/// 把窗口类背景刷成主题底色，避免启动/缩放瞬间闪白。
void SetWindowBackground(tauricpp::Window& window, const std::string& theme_id);

}  // namespace silkit::ipc
