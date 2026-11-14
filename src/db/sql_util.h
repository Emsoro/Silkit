#pragma once

#include "db/sql_splitter.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace silkit::db {

/// 读取第一条 SQL 关键字（跳过注释与空白），返回大写形式。
std::string FirstSqlKeyword(const std::string& sql);

/// SELECT/PRAGMA/EXPLAIN/WITH/VALUES 等只读语句。
bool IsReadOnlyStatement(const std::string& sql);

/// 破坏性语句（DROP/TRUNCATE/ALTER/无 WHERE 的 DELETE/UPDATE 等）。
bool IsDestructiveStatement(const std::string& sql);

/// 安全模式命中项：语句序号（0 开始）与原文。
struct DestructiveHit {
    std::size_t index = 0;
    std::string sql;
};

/// 找出语句列表中的危险语句（按原顺序）。
std::vector<DestructiveHit> FindDestructiveStatements(const std::vector<SqlStatement>& statements);

/// 危险确认令牌：对全部语句的 `index:text` 做 FNV-1a。
/// 前端确认后用同一批语句重试；语句变化则令牌失效。
std::uint64_t DestructiveToken(const std::vector<SqlStatement>& statements);

}  // namespace silkit::db
