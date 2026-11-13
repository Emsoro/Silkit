#pragma once

#include "db/types.h"

#include <string>

namespace silkit::db {

/// 根据声明类型推断 UI 语义类型（用于对齐、编辑器和复制格式）。
ValueKind KindFromDeclType(const std::string& decl_type);

/// SQLite 动态类型的实际值类型 + 声明类型提示。
ValueKind KindFromSqliteType(int sqlite_type, const std::string& decl_type);

}  // namespace silkit::db
