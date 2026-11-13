#pragma once

#include "db/session_manager.h"
#include "ipc/async_router.h"

namespace silkit::ipc {

void RegisterMetadataCommands(AsyncRouter& router, session::SessionManager& sessions);

}  // namespace silkit::ipc
