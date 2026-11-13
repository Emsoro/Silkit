#pragma once

#include "db/driver.h"
#include "db/session.h"
#include "db/session_manager.h"
#include "ipc/async_router.h"

#include <memory>
#include <string>
#include <vector>

namespace silkit::ipc::dbutil {

using json = nlohmann::json;

/// profile JSON → ConnectParams。password 参数可覆盖配置文件中的密码。
db::ConnectParams ParamsFromProfile(const json& profile, const std::string& password, std::string& error);

json CapabilitiesJson(const db::Capabilities& caps);
json ErrorJson(const db::DbError& error);
json ColumnJson(const db::ColumnMeta& column);
json CellValueJson(const db::Cell& cell);

[[noreturn]] void ThrowCommandError(const std::string& code, const std::string& message);

std::shared_ptr<session::Session> RequireSession(const session::SessionManager& sessions,
                                                 const std::string& session_id);

struct RowsResult {
    db::ExecResult result;
    std::vector<std::vector<db::Cell>> rows;
    bool ok() const { return result.error.code == 0; }
};

RowsResult RunRows(db::Connection& connection,
                   const std::string& sql,
                   const std::vector<db::Param>& params = {});

std::string CellText(const db::Cell& cell);
std::string NodeNameFromCell(const db::Cell& cell);
std::int64_t CellInt(const db::Cell& cell, std::int64_t fallback = 0);

}  // namespace silkit::ipc::dbutil
