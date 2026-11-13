#pragma once

#include "db/types.h"

#include <string>

namespace silkit::db {

/// 按引擎引用标识符：SQLite/PostgreSQL 用双引号，MySQL 用反引号。
std::string QuoteIdentifier(Engine engine, const std::string& name);

/// SQL 字符串字面量（单引号转义）。仅用于 PRAGMA 等无法参数化的元数据 SQL。
std::string QuoteStringLiteral(const std::string& value);

/// 标识符基本校验：非空、不含控制字符、长度 <= 255。
bool IsSafeIdentifier(const std::string& name);

}  // namespace silkit::db
