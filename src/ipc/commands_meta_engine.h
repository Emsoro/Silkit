#pragma once

#include "db/driver.h"
#include "ipc/async_router.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace silkit::ipc {

/// SQLite 之外的引擎（MySQL/MariaDB、PostgreSQL）走本模块；
/// 返回 false 时应沿用 commands_meta.cpp 的 sqlite_master/PRAGMA 实现。
bool IsRelationalEngine(db::Engine engine);

json EngineMetaChildren(db::Connection& connection, const json& parent, std::uint64_t epoch);
json EngineMetaSearch(db::Connection& connection, const std::string& keyword,
                      int limit, const std::vector<std::string>& kinds, std::uint64_t epoch);
json EngineMetaDetail(db::Connection& connection, const json& ref);
json EngineMetaDdl(db::Connection& connection, const json& ref);

/// conn.stats 用：一次查询取回 {字符集, 时区}；非关系引擎返回空。
std::pair<std::string, std::string> EngineServerStats(db::Connection& connection);

}  // namespace silkit::ipc
