#pragma once

#include "core/profiles.h"
#include "db/session_manager.h"
#include "ipc/async_router.h"

namespace silkit::ipc {

void RegisterConnectionCommands(AsyncRouter& router,
                                ProfileStore& profiles,
                                session::SessionManager& sessions);

}  // namespace silkit::ipc
