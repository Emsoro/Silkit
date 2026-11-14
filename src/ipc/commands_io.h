#pragma once

#include "core/settings.h"
#include "db/session_manager.h"
#include "ipc/async_router.h"

namespace silkit::ipc {

/// M7：导入/导出/转储/执行 SQL 文件。
void RegisterIoCommands(AsyncRouter& router, session::SessionManager& sessions, Settings& settings);

}  // namespace silkit::ipc
