#pragma once

#include "db/driver.h"

#include <string>
#include <vector>

namespace silkit::db {

/// 表设计器的纯 DB 逻辑：定义加载、DDL 组装、令牌与校验。
/// IPC 层只负责 JSON 编解码、事务与执行，便于三引擎共用同一套生成器。

struct DdlColumn {
    std::string name;
    std::string type;
    bool not_null = false;
    bool primary_key = false;
    bool auto_increment = false;
    bool has_default = false;
    std::string default_expr;
    std::string comment;
    std::string old_name;  // 非空表示从旧列名重命名
};

struct DdlIndex {
    std::string name;
    std::vector<std::string> columns;
    bool unique = false;
};

struct DdlForeignKey {
    std::string name;
    std::vector<std::string> columns;
    std::string ref_table;
    std::string ref_schema;
    std::vector<std::string> ref_columns;
    std::string on_update = "NO ACTION";
    std::string on_delete = "NO ACTION";
};

struct DdlTableOptions {
    std::string engine;  // MySQL 存储引擎
    std::string charset;
    std::string collation;
    std::string comment;
};

struct DdlTableDefinition {
    Engine engine = Engine::Sqlite;
    std::string scope;  // MySQL 库 / PG schema / SQLite 附加库
    std::string name;
    std::vector<DdlColumn> columns;
    std::vector<DdlIndex> indexes;
    std::vector<DdlForeignKey> foreign_keys;
    DdlTableOptions options;
    std::string primary_key_name;
    bool exists = false;
};

struct DdlPlan {
    std::vector<std::string> statements;
    std::vector<std::string> warnings;
};

/// 从目录读取现有表定义（列/索引/外键/选项）。
bool LoadDdlDefinition(Connection& connection, const std::string& scope,
                       const std::string& table, DdlTableDefinition& out, DbError& error);

/// 设计器类型下拉的推荐类型（原始声明类型文本）。
std::vector<std::string> DdlTypeSuggestions(Engine engine);

enum class DdlMode {
    Create,
    Alter,
    Drop,
    Truncate,
    CreateDatabase,
    DropDatabase,
};

DdlMode DdlModeFromName(const std::string& name, bool& ok);

/// 根据模式组装 DDL。alter 时 original 必须来自数据库（或 ddl.load）。
/// new_name 仅用于表重命名 / 建库与删库。
bool BuildDdlPlan(DdlMode mode, Engine engine, const std::string& scope,
                  const DdlTableDefinition& original, const DdlTableDefinition& updated,
                  const std::string& new_name, DdlPlan& out, std::string& error);

/// 预览令牌：plan 内容一旦变化，旧令牌立即失效。
std::string DdlPlanToken(const DdlPlan& plan);

/// 复杂 DDL 是否可放进事务（MySQL DDL 隐式提交，返回 false）。
bool DdlTransactional(Engine engine, DdlMode mode);

/// 校验表/列/索引名与表达式，供 IPC 层在组装前给出准确错误。
bool ValidateDefinition(const DdlTableDefinition& definition, std::string& error);

}  // namespace silkit::db
