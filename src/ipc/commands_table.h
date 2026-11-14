#pragma once

#include "core/settings.h"
#include "db/session_manager.h"
#include "ipc/async_router.h"

namespace silkit::ipc {

/// M5：表数据浏览/CRUD/单元格读取。
void RegisterTableCommands(AsyncRouter& router,
                           session::SessionManager& sessions,
                           Settings& settings);

}  // namespace silkit::ipc
