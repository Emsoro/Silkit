#pragma once

#include <string>

namespace silkit::db {

/// 读取第一条 SQL 关键字（跳过注释与空白），返回大写形式。
std::string FirstSqlKeyword(const std::string& sql);

/// SELECT/PRAGMA/EXPLAIN/WITH/VALUES 等只读语句。
bool IsReadOnlyStatement(const std::string& sql);

/// 破坏性语句（DROP/TRUNCATE/ALTER/无 WHERE 的 DELETE/UPDATE 等）的安全模式检测
/// 由 UI 在 M8 进一步细化；这里提供基础分类供只读连接使用。
bool IsDestructiveStatement(const std::string& sql);

}  // namespace silkit::db
