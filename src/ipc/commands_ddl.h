#pragma once

#include "db/session_manager.h"
#include "ipc/async_router.h"

namespace silkit::ipc {

/// M6：表设计器与 DDL 生成/执行。
void RegisterDdlCommands(AsyncRouter& router, session::SessionManager& sessions);

}  // namespace silkit::ipc
