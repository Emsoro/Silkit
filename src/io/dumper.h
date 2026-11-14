#pragma once

#include "db/driver.h"
#include "io/encoding.h"

#include <cstdint>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

namespace silkit::io {

/// M7-6：整库/表 SQL 转储（结构 + 数据，可带 DROP IF EXISTS）。
struct DumpOptions {
    db::Engine engine = db::Engine::Sqlite;
    std::string scope;  // MySQL 库 / PG schema / SQLite 附加库；空表示默认
    std::vector<std::string> tables;  // 空表示 scope 下全部表
    bool include_structure = true;
    bool include_data = true;
    bool drop_if_exists = true;
    std::string null_literal;
    TextEncoding encoding = TextEncoding::Utf8;
};

struct DumpResult {
    std::uint64_t tables = 0;
    std::uint64_t rows = 0;
    std::uint64_t bytes = 0;
};

enum class DumpStatus { Ok, Cancelled, Failed };

using DumpProgress =
    std::function<void(std::size_t done, std::size_t total, const std::string& table,
                       std::uint64_t rows)>;

DumpStatus DumpSql(db::Connection& connection, const DumpOptions& options, std::ostream& out,
                   DumpResult& result, std::string& error,
                   const DumpProgress& progress = {}, const std::function<bool()>& cancelled = {});

}  // namespace silkit::io
