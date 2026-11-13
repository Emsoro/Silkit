#pragma once

#include "core/settings.h"
#include "db/session_manager.h"
#include "ipc/async_router.h"

namespace silkit::ipc {

void RegisterQueryCommands(AsyncRouter& router,
                           session::SessionManager& sessions,
                           Settings& settings);

}  // namespace silkit::ipc
