#pragma once

#include "db/types.h"

#include <string>
#include <vector>

namespace silkit::db {

/// 连接参数默认值与方言差异集中在这里，驱动与 IPC 层共用。
const char* DefaultPort(Engine engine);
std::string DefaultDatabase(Engine engine);

/// 把驱动原始错误补上可读提示（detail/hint），不覆盖服务端原文。
void DecorateError(Engine engine, DbError& error);

/// MySQL/MariaDB：libmariadb 的 enum_field_types + flags → UI 语义类型。
ValueKind KindFromMySqlFieldType(int field_type, unsigned int flags,
                                 const std::string& decl_type);

/// PostgreSQL：pg_type OID → UI 语义类型；未知回退 Text。
ValueKind KindFromPgOid(unsigned int oid);

/// 元数据 SQL。scope 对 MySQL 是数据库名，对 PG 是 schema 名。
/// 所有标识符都经过 QuoteStringLiteral 内联，避免占位符方言差异。
std::string ListDatabasesSql(Engine engine);
std::string ListSchemasSql();
std::string ListObjectNamesSql(Engine engine, const std::string& scope,
                               const std::string& folder);
std::string ListColumnsSql(Engine engine, const std::string& scope,
                           const std::string& table);
std::string ListIndexesSql(Engine engine, const std::string& scope,
                           const std::string& table);
std::string ListForeignKeysSql(Engine engine, const std::string& scope,
                               const std::string& table);
std::string ListTriggersSql(Engine engine, const std::string& scope,
                             const std::string& table);
std::string ListPrimaryKeysSql(Engine engine, const std::string& scope,
                               const std::string& table);
std::string DdlSql(Engine engine, const std::string& kind,
                    const std::string& scope, const std::string& name);
std::string SearchObjectsSql(Engine engine, const std::string& scope,
                             const std::string& keyword, int limit);

/// MySQL 用 USE 切库；PG 需要新建连接，返回空串表示不支持原地切换。
std::string SwitchDatabaseSql(Engine engine, const std::string& database);

/// conn.stats 的字符集/时区/版本查询（一行三列）。
std::string ServerStatsSql(Engine engine);

}  // namespace silkit::db
