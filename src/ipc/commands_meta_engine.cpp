#include "ipc/commands_meta_engine.h"

#include "db/engine_dialect.h"
#include "db/identifier.h"
#include "db/type_map.h"
#include "db/types.h"
#include "ipc/commands_db_common.h"

#include <algorithm>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace silkit::ipc {
namespace {

using dbutil::CellText;
using dbutil::RunRows;

struct Scope {
    db::Engine engine = db::Engine::Sqlite;
    std::string kind;
    std::string name;
    std::string database;
    std::string schema;
    std::string table;
};

json MakeNode(const std::string& kind, const std::string& name, const Scope& scope,
              bool has_children, const std::string& badge = std::string{},
              const std::string& table = std::string{}) {
    json node{{"kind", kind},
              {"name", name},
              {"database", scope.database},
              {"schema", scope.schema},
              {"hasChildren", has_children},
              {"expandable", has_children}};
    if (!badge.empty()) node["badge"] = badge;
    if (!table.empty()) node["table"] = table;
    return node;
}

json FolderNode(const std::string& name, const Scope& scope,
                const std::string& badge = std::string{}) {
    return MakeNode("folder", name, scope, true, badge, scope.table);
}

std::string ObjectScope(const Scope& scope) {
    return scope.engine == db::Engine::MySql ? scope.database : scope.schema;
}

Scope ParseScope(const json& parent, db::Connection& connection) {
    Scope scope;
    scope.engine = connection.EngineKind();
    scope.kind = parent.value("kind", std::string{"connection"});
    scope.name = parent.value("name", std::string{});
    scope.database = parent.value("database", std::string{});
    scope.schema = parent.value("schema", std::string{});
    scope.table = parent.value("table", std::string{});
    if (scope.database.empty()) scope.database = connection.CurrentDatabase();
    if (scope.engine == db::Engine::Postgres) {
        if (scope.schema.empty() && scope.kind == "database") scope.schema = scope.name;
        if (scope.schema.empty()) scope.schema = "public";
    }
    return scope;
}

void RequireOk(const dbutil::RowsResult& rows) {
    if (rows.ok()) return;
    throw IpcError(dbutil::ErrorJson(rows.result.error));
}

std::string RowText(const std::vector<db::Cell>& row, std::size_t index) {
    return index < row.size() ? CellText(row[index]) : std::string{};
}

std::string KindForFolder(const std::string& folder) {
    if (folder == "tables") return "table";
    if (folder == "views") return "view";
    if (folder == "indexes") return "index";
    if (folder == "triggers") return "trigger";
    if (folder == "routines") return "routine";
    if (folder == "events") return "event";
    if (folder == "sequences") return "sequence";
    if (folder == "databases") return "database";
    return "object";
}

bool IsTrue(const std::string& value) {
    return value == "t" || value == "true" || value == "1" || value == "YES";
}

json PrimaryKeyJson(const std::set<std::string>& keys) {
    json array = json::array();
    for (const auto& key : keys) array.push_back(key);
    return array;
}

json ObjectNodes(db::Connection& connection, const std::string& folder, const Scope& scope) {
    json nodes = json::array();
    if (folder == "databases" && scope.engine == db::Engine::Postgres) {
        auto rows = RunRows(connection, db::ListDatabasesSql(scope.engine));
        RequireOk(rows);
        for (const auto& row : rows.rows) {
            Scope child = scope;
            child.database = RowText(row, 0);
            nodes.push_back(MakeNode("database", child.database, child, false, "需重新连接"));
        }
        return nodes;
    }

    const std::string sql = db::ListObjectNamesSql(scope.engine, ObjectScope(scope), folder);
    if (sql.empty()) return nodes;
    auto rows = RunRows(connection, sql);
    RequireOk(rows);
    const std::string kind = KindForFolder(folder);
    const bool expandable = kind == "table" || kind == "view";
    for (const auto& row : rows.rows) {
        const std::string name = RowText(row, 0);
        const std::string owner = row.size() > 1 ? RowText(row, 1) : std::string{};
        const std::string table = (kind == "index" || kind == "trigger") ? owner : std::string{};
        nodes.push_back(MakeNode(kind, name, scope, expandable, owner, table));
    }
    return nodes;
}

json TableFolderChildren(db::Connection& connection, const Scope& scope,
                          const std::string& folder) {
    json nodes = json::array();
    const std::string object_scope = ObjectScope(scope);

    if (folder == "columns") {
        std::set<std::string> primary_keys;
        auto pk_rows = RunRows(connection, db::ListPrimaryKeysSql(scope.engine, object_scope, scope.table));
        if (pk_rows.ok()) {
            for (const auto& row : pk_rows.rows) primary_keys.insert(RowText(row, 0));
        }
        auto rows = RunRows(connection, db::ListColumnsSql(scope.engine, object_scope, scope.table));
        RequireOk(rows);
        for (const auto& row : rows.rows) {
            const std::string name = RowText(row, 0);
            const std::string type = RowText(row, 1);
            std::string badge = type;
            if (primary_keys.count(name) != 0) badge += " · PK";
            nodes.push_back(MakeNode("column", name, scope, false, badge, scope.table));
        }
        return nodes;
    }

    if (folder == "indexes") {
        auto rows = RunRows(connection, db::ListIndexesSql(scope.engine, object_scope, scope.table));
        RequireOk(rows);
        std::set<std::string> seen;
        for (const auto& row : rows.rows) {
            const std::string name = RowText(row, 0);
            if (!seen.insert(name).second) continue;  // MySQL 每个索引列一行
            std::string badge;
            if (scope.engine == db::Engine::MySql) {
                badge = IsTrue(RowText(row, 2)) ? "" : "唯一";
            } else {
                badge = IsTrue(RowText(row, 2)) ? "唯一" : "";
            }
            nodes.push_back(MakeNode("index", name, scope, false, badge, scope.table));
        }
        return nodes;
    }

    if (folder == "foreign_keys") {
        auto rows = RunRows(connection, db::ListForeignKeysSql(scope.engine, object_scope, scope.table));
        RequireOk(rows);
        for (const auto& row : rows.rows) {
            if (scope.engine == db::Engine::MySql) {
                const std::string badge = "→ " + RowText(row, 2) + "." + RowText(row, 3);
                json node = MakeNode("foreign_key", RowText(row, 1), scope, false, badge, scope.table);
                node["column"] = RowText(row, 1);
                node["refTable"] = RowText(row, 2);
                node["refColumn"] = RowText(row, 3);
                nodes.push_back(std::move(node));
            } else {
                json node = MakeNode("foreign_key", RowText(row, 0), scope, false,
                                     RowText(row, 1), scope.table);
                nodes.push_back(std::move(node));
            }
        }
        return nodes;
    }

    if (folder == "triggers") {
        auto rows = RunRows(connection, db::ListTriggersSql(scope.engine, object_scope, scope.table));
        RequireOk(rows);
        for (const auto& row : rows.rows) {
            nodes.push_back(MakeNode("trigger", RowText(row, 0), scope, false,
                                     RowText(row, 1), scope.table));
        }
        return nodes;
    }

    return nodes;
}

json TableFolderNodes(const Scope& scope) {
    json nodes = json::array();
    nodes.push_back(FolderNode("columns", scope, "列"));
    nodes.push_back(FolderNode("indexes", scope, "索引"));
    if (scope.kind != "view") nodes.push_back(FolderNode("foreign_keys", scope, "外键"));
    nodes.push_back(FolderNode("triggers", scope, "触发器"));
    return nodes;
}

json DatabaseFolderNodes(const Scope& scope) {
    json nodes = json::array();
    nodes.push_back(FolderNode("tables", scope, "表"));
    nodes.push_back(FolderNode("views", scope, "视图"));
    nodes.push_back(FolderNode("indexes", scope, "索引"));
    nodes.push_back(FolderNode("routines", scope, "函数/过程"));
    nodes.push_back(FolderNode("triggers", scope, "触发器"));
    if (scope.engine == db::Engine::MySql) {
        nodes.push_back(FolderNode("events", scope, "事件"));
    } else {
        nodes.push_back(FolderNode("sequences", scope, "序列"));
    }
    return nodes;
}

json ColumnJsonForEngine(const std::vector<db::Cell>& row, const Scope& scope,
                         const std::set<std::string>& primary_keys) {
    const std::string name = RowText(row, 0);
    const std::string type = RowText(row, 1);
    const bool not_null = scope.engine == db::Engine::MySql
                              ? RowText(row, 2) == "NO"
                              : IsTrue(RowText(row, 2));
    const std::string default_expr = RowText(row, 3);
    const bool primary = primary_keys.count(name) != 0;
    return json{{"name", name},
                {"type", type},
                {"kind", db::KindName(db::KindFromDeclType(type))},
                {"notNull", not_null},
                {"default", default_expr.empty() ? json(nullptr) : json(default_expr)},
                {"primaryKey", primary},
                {"hidden", 0},
                {"ordinal", row.size() > 7 ? dbutil::CellInt(row[7], 0) : 0}};
}

json BuildPgCreateTable(db::Connection& connection, const Scope& scope) {
    std::set<std::string> primary_keys;
    auto pk_rows = RunRows(connection, db::ListPrimaryKeysSql(scope.engine, ObjectScope(scope), scope.name));
    if (pk_rows.ok()) {
        for (const auto& row : pk_rows.rows) primary_keys.insert(RowText(row, 0));
    }
    auto columns = RunRows(connection, db::ListColumnsSql(scope.engine, ObjectScope(scope), scope.name));
    RequireOk(columns);

    std::string sql = "CREATE TABLE " + db::QuoteIdentifier(db::Engine::Postgres, ObjectScope(scope)) +
                      "." + db::QuoteIdentifier(db::Engine::Postgres, scope.name) + " (\n";
    bool first = true;
    for (const auto& row : columns.rows) {
        const std::string name = RowText(row, 0);
        const std::string type = RowText(row, 1);
        const bool not_null = IsTrue(RowText(row, 2));
        const std::string default_expr = RowText(row, 3);
        if (!first) sql += ",\n";
        first = false;
        sql += "  " + db::QuoteIdentifier(db::Engine::Postgres, name) + " " + type;
        if (not_null) sql += " NOT NULL";
        if (!default_expr.empty()) sql += " DEFAULT " + default_expr;
    }
    if (!primary_keys.empty()) {
        if (!first) sql += ",\n";
        sql += "  PRIMARY KEY (";
        bool first_pk = true;
        for (const auto& name : primary_keys) {
            if (!first_pk) sql += ", ";
            first_pk = false;
            sql += db::QuoteIdentifier(db::Engine::Postgres, name);
        }
        sql += ")";
    }
    sql += "\n);\n";
    return json{{"sql", sql},
                {"warnings", json::array({"由 pg_catalog 目录重建；索引/外键/触发器/注释请查看详情面板",
                                          "与 pg_dump --schema-only 可能存在差异（M3-5 简化实现）"})}};
}

json DdlForObject(db::Connection& connection, const json& ref) {
    Scope scope = ParseScope(ref, connection);
    const std::string kind = ref.value("kind", std::string{"table"});
    const std::string name = ref.value("name", std::string{});
    if (name.empty()) dbutil::ThrowCommandError("bad_args", "缺少对象名称");

    if (scope.engine == db::Engine::Postgres && kind == "table") {
        return BuildPgCreateTable(connection, scope);
    }

    const std::string sql = db::DdlSql(scope.engine, kind, ObjectScope(scope), name);
    if (sql.empty()) dbutil::ThrowCommandError("bad_args", "该对象没有 DDL: " + kind);
    auto rows = RunRows(connection, sql);
    RequireOk(rows);
    if (rows.rows.empty()) {
        dbutil::ThrowCommandError("not_found", "对象不存在或没有 SQL 定义: " + name);
    }
    const std::size_t column = (scope.engine == db::Engine::MySql &&
                                (kind == "table" || kind == "view")) ? 1 : 0;
    const std::string ddl = RowText(rows.rows.front(), column);
    if (ddl.empty()) {
        dbutil::ThrowCommandError("not_found", "对象不存在或没有 SQL 定义: " + name);
    }
    return json{{"sql", ddl}, {"warnings", json::array()}};
}

json DetailForTable(db::Connection& connection, const json& ref) {
    Scope scope = ParseScope(ref, connection);
    scope.name = ref.value("name", scope.name);
    scope.table = scope.name;
    const std::string object_scope = ObjectScope(scope);

    std::set<std::string> primary_keys;
    auto pk_rows = RunRows(connection, db::ListPrimaryKeysSql(scope.engine, object_scope, scope.name));
    if (pk_rows.ok()) {
        for (const auto& row : pk_rows.rows) primary_keys.insert(RowText(row, 0));
    }

    json columns = json::array();
    auto column_rows = RunRows(connection, db::ListColumnsSql(scope.engine, object_scope, scope.name));
    RequireOk(column_rows);
    for (const auto& row : column_rows.rows) {
        columns.push_back(ColumnJsonForEngine(row, scope, primary_keys));
    }

    json indexes = json::array();
    auto index_rows = RunRows(connection, db::ListIndexesSql(scope.engine, object_scope, scope.name));
    if (index_rows.ok()) {
        std::set<std::string> seen;
        for (const auto& row : index_rows.rows) {
            const std::string name = RowText(row, 0);
            if (!seen.insert(name).second) continue;
            const bool unique = scope.engine == db::Engine::MySql
                                    ? !IsTrue(RowText(row, 2))
                                    : IsTrue(RowText(row, 2));
            indexes.push_back(json{{"name", name},
                                   {"unique", unique},
                                   {"origin", ""},
                                   {"partial", false},
                                   {"columns", json::array()}});
        }
    }

    json foreign_keys = json::array();
    auto fk_rows = RunRows(connection, db::ListForeignKeysSql(scope.engine, object_scope, scope.name));
    if (fk_rows.ok()) {
        for (const auto& row : fk_rows.rows) {
            if (scope.engine == db::Engine::MySql) {
                foreign_keys.push_back(json{{"name", RowText(row, 0)},
                                           {"from", RowText(row, 1)},
                                           {"table", RowText(row, 2)},
                                           {"to", RowText(row, 3)},
                                           {"onUpdate", RowText(row, 4)},
                                           {"onDelete", RowText(row, 5)}});
            } else {
                foreign_keys.push_back(json{{"name", RowText(row, 0)},
                                           {"definition", RowText(row, 1)}});
            }
        }
    }

    json triggers = json::array();
    auto trigger_rows = RunRows(connection, db::ListTriggersSql(scope.engine, object_scope, scope.name));
    if (trigger_rows.ok()) {
        for (const auto& row : trigger_rows.rows) {
            triggers.push_back(json{{"name", RowText(row, 0)}, {"sql", RowText(row, 1)}});
        }
    }

    std::string ddl;
    try {
        ddl = DdlForObject(connection, ref).value("sql", std::string{});
    } catch (...) {
        ddl.clear();  // DDL 不可用不影响详情面板
    }

    return json{{"database", scope.database},
                {"schema", scope.schema},
                {"name", scope.name},
                {"columns", columns},
                {"primaryKey", PrimaryKeyJson(primary_keys)},
                {"indexes", indexes},
                {"foreignKeys", foreign_keys},
                {"triggers", triggers},
                {"sql", ddl}};
}

}  // namespace

bool IsRelationalEngine(db::Engine engine) {
    return engine == db::Engine::MySql || engine == db::Engine::Postgres;
}

json EngineMetaChildren(db::Connection& connection, const json& parent, std::uint64_t epoch) {
    Scope scope = ParseScope(parent, connection);
    json nodes = json::array();

    if (scope.kind == "connection" || scope.kind.empty()) {
        if (scope.engine == db::Engine::MySql) {
            auto rows = RunRows(connection, db::ListDatabasesSql(scope.engine));
            RequireOk(rows);
            for (const auto& row : rows.rows) {
                Scope child = scope;
                child.database = RowText(row, 0);
                nodes.push_back(MakeNode("database", child.database, child, true));
            }
        } else {
            nodes.push_back(FolderNode("databases", scope, "库"));
            auto rows = RunRows(connection, db::ListSchemasSql());
            RequireOk(rows);
            for (const auto& row : rows.rows) {
                Scope child = scope;
                child.schema = RowText(row, 0);
                child.database = connection.CurrentDatabase();
                nodes.push_back(MakeNode("database", child.schema, child, true));
            }
        }
    } else if (scope.kind == "database") {
        nodes = DatabaseFolderNodes(scope);
    } else if (scope.kind == "folder") {
        if (!scope.table.empty()) {
            nodes = TableFolderChildren(connection, scope, scope.name);
        } else {
            nodes = ObjectNodes(connection, scope.name, scope);
        }
    } else if (scope.kind == "table" || scope.kind == "view") {
        Scope child = scope;
        child.table = scope.name;
        nodes = TableFolderNodes(child);
    }

    return json{{"nodes", nodes}, {"epoch", epoch}};
}

json EngineMetaSearch(db::Connection& connection, const std::string& keyword,
                       int limit, const std::vector<std::string>& kinds, std::uint64_t epoch) {
    json nodes = json::array();
    if (keyword.empty()) return json{{"nodes", nodes}, {"total", 0}, {"epoch", epoch}};

    std::vector<std::string> scopes;
    const std::string list_sql = connection.EngineKind() == db::Engine::MySql
                                     ? db::ListDatabasesSql(db::Engine::MySql)
                                     : db::ListSchemasSql();
    auto scope_rows = RunRows(connection, list_sql);
    if (scope_rows.ok()) {
        for (const auto& row : scope_rows.rows) scopes.push_back(RowText(row, 0));
    }
    if (scopes.empty()) scopes.push_back(ObjectScope(ParseScope(json::object(), connection)));

    std::size_t remaining = static_cast<std::size_t>(limit);
    const std::size_t max_scopes = std::min<std::size_t>(scopes.size(), 50);
    for (std::size_t i = 0; i < max_scopes && remaining > 0; ++i) {
        Scope scope;
        scope.engine = connection.EngineKind();
        scope.database = scope.engine == db::Engine::MySql ? scopes[i] : connection.CurrentDatabase();
        scope.schema = scope.engine == db::Engine::Postgres ? scopes[i] : std::string{};
        const std::string sql = db::SearchObjectsSql(scope.engine, ObjectScope(scope), keyword,
                                                      static_cast<int>(remaining));
        auto rows = RunRows(connection, sql);
        if (!rows.ok()) continue;
        for (const auto& row : rows.rows) {
            const std::string kind = RowText(row, 1);
            if (!kinds.empty() && std::find(kinds.begin(), kinds.end(), kind) == kinds.end()) continue;
            nodes.push_back(MakeNode(kind, RowText(row, 0), scope, true, kind));
            --remaining;
            if (remaining == 0) break;
        }
    }
    return json{{"nodes", nodes}, {"total", nodes.size()}, {"epoch", epoch}};
}

json EngineMetaDetail(db::Connection& connection, const json& ref) {
    const std::string kind = ref.value("kind", std::string{"table"});
    if (kind == "table" || kind == "view") return DetailForTable(connection, ref);
    return DdlForObject(connection, ref);
}

json EngineMetaDdl(db::Connection& connection, const json& ref) {
    return DdlForObject(connection, ref);
}

std::pair<std::string, std::string> EngineServerStats(db::Connection& connection) {
    if (!IsRelationalEngine(connection.EngineKind())) return {};
    auto rows = RunRows(connection, db::ServerStatsSql(connection.EngineKind()));
    if (!rows.ok() || rows.rows.empty()) return {};
    return {RowText(rows.rows.front(), 0), RowText(rows.rows.front(), 1)};
}

}  // namespace silkit::ipc
