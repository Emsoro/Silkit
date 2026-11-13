#include "ipc/commands_meta.h"

#include "db/identifier.h"
#include "db/type_map.h"
#include "db/types.h"
#include "ipc/commands_db_common.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace silkit::ipc {
namespace {

using dbutil::json;

std::string MasterTable(const std::string& database) {
    return db::QuoteIdentifier(db::Engine::Sqlite, database) + ".sqlite_master";
}

std::string Pragma(const std::string& database, const char* pragma, const std::string& argument) {
    return "PRAGMA " + db::QuoteIdentifier(db::Engine::Sqlite, database) + "." + pragma + "(" +
           db::QuoteStringLiteral(argument) + ")";
}

json MakeNode(const std::string& kind,
              const std::string& name,
              const std::string& database,
              bool has_children,
              const std::string& badge = std::string{},
              const std::string& table = std::string{}) {
    json node{{"kind", kind},
              {"name", name},
              {"database", database},
              {"schema", ""},
              {"hasChildren", has_children},
              {"expandable", has_children}};
    if (!badge.empty()) node["badge"] = badge;
    if (!table.empty()) node["table"] = table;
    return node;
}

json FolderNode(const std::string& name, const std::string& database, const std::string& badge) {
    return MakeNode("folder", name, database, true, badge);
}

std::string RowText(const std::vector<db::Cell>& row, std::size_t index) {
    if (index >= row.size()) return {};
    return dbutil::CellText(row[index]);
}

std::int64_t RowInt(const std::vector<db::Cell>& row, std::size_t index, std::int64_t fallback = 0) {
    if (index >= row.size()) return fallback;
    return dbutil::CellInt(row[index], fallback);
}

void RequireOk(const dbutil::RowsResult& rows) {
    if (rows.ok()) return;
    json payload = dbutil::ErrorJson(rows.result.error);
    throw IpcError(payload);
}

std::unordered_map<std::string, int> CountObjects(db::Connection& connection, const std::string& database) {
    const std::string sql = "SELECT type, COUNT(*) FROM " + MasterTable(database) +
                            " WHERE type IN ('table','view','index','trigger')"
                            " AND name NOT LIKE 'sqlite_%' GROUP BY type";
    auto rows = dbutil::RunRows(connection, sql);
    RequireOk(rows);
    std::unordered_map<std::string, int> counts;
    for (const auto& row : rows.rows) {
        counts[RowText(row, 0)] = static_cast<int>(RowInt(row, 1));
    }
    return counts;
}

json DatabaseChildren(db::Connection& connection) {
    auto rows = dbutil::RunRows(connection, "PRAGMA database_list");
    RequireOk(rows);
    json nodes = json::array();
    for (const auto& row : rows.rows) {
        const std::string name = RowText(row, 1);
        const std::string file = RowText(row, 2);
        std::string badge = file.empty() ? "内存" : file;
        nodes.push_back(MakeNode("database", name, name, true, badge));
    }
    return nodes;
}

json FolderChildren(db::Connection& connection, const std::string& database, const std::string& folder) {
    const std::string master = MasterTable(database);
    json nodes = json::array();

    if (folder == "tables" || folder == "views") {
        const char* type = folder == "tables" ? "table" : "view";
        const std::string sql = "SELECT name FROM " + master + " WHERE type = '" + type +
                                "' AND name NOT LIKE 'sqlite_%' ORDER BY name";
        auto rows = dbutil::RunRows(connection, sql);
        RequireOk(rows);
        for (const auto& row : rows.rows) {
            nodes.push_back(MakeNode(type, RowText(row, 0), database, true));
        }
        return nodes;
    }

    if (folder == "indexes") {
        const std::string sql = "SELECT name, tbl_name FROM " + master +
                                " WHERE type = 'index' AND name NOT LIKE 'sqlite_%' ORDER BY name";
        auto rows = dbutil::RunRows(connection, sql);
        RequireOk(rows);
        for (const auto& row : rows.rows) {
            nodes.push_back(MakeNode("index", RowText(row, 0), database, false, RowText(row, 1)));
        }
        return nodes;
    }

    if (folder == "triggers") {
        const std::string sql =
            "SELECT name, tbl_name FROM " + master + " WHERE type = 'trigger' ORDER BY name";
        auto rows = dbutil::RunRows(connection, sql);
        RequireOk(rows);
        for (const auto& row : rows.rows) {
            nodes.push_back(MakeNode("trigger", RowText(row, 0), database, false, RowText(row, 1)));
        }
        return nodes;
    }

    if (folder == "foreign_keys") {
        // 由表节点展开时处理（需要表名）；这里返回空。
        return nodes;
    }

    return nodes;
}

json TableChildren(db::Connection& connection,
                   const std::string& database,
                   const std::string& table,
                   bool is_view) {
    json nodes = json::array();
    nodes.push_back(FolderNode("columns", database, "列"));
    if (!is_view) {
        nodes.push_back(FolderNode("indexes", database, "索引"));
        nodes.push_back(FolderNode("foreign_keys", database, "外键"));
    }
    nodes.push_back(FolderNode("triggers", database, "触发器"));
    (void)connection;
    (void)table;
    return nodes;
}

json TableFolderChildren(db::Connection& connection,
                         const std::string& database,
                         const std::string& table,
                         const std::string& folder) {
    json nodes = json::array();

    if (folder == "columns") {
        auto rows = dbutil::RunRows(connection, Pragma(database, "table_xinfo", table));
        RequireOk(rows);
        for (const auto& row : rows.rows) {
            const std::int64_t hidden = RowInt(row, 6, 0);
            if (hidden == 1) continue;  // 虚拟表的隐藏列
            const std::string name = RowText(row, 1);
            const std::string type = RowText(row, 2);
            std::string badge = type;
            if (RowInt(row, 5, 0) > 0) badge += " · PK";
            nodes.push_back(MakeNode("column", name, database, false, badge, table));
        }
        return nodes;
    }

    if (folder == "indexes") {
        auto rows = dbutil::RunRows(connection, Pragma(database, "index_list", table));
        RequireOk(rows);
        for (const auto& row : rows.rows) {
            const std::string name = RowText(row, 1);
            const bool unique = RowInt(row, 2, 0) != 0;
            nodes.push_back(MakeNode("index", name, database, false, unique ? "唯一" : "", table));
        }
        return nodes;
    }

    if (folder == "foreign_keys") {
        auto rows = dbutil::RunRows(connection, Pragma(database, "foreign_key_list", table));
        RequireOk(rows);
        for (const auto& row : rows.rows) {
            const std::string from = RowText(row, 3);
            const std::string target_table = RowText(row, 2);
            const std::string target_column = RowText(row, 4);
            json node = MakeNode("foreign_key", from, database, false,
                                 "→ " + target_table + "." + target_column, table);
            node["column"] = from;
            node["refTable"] = target_table;
            node["refColumn"] = target_column;
            nodes.push_back(std::move(node));
        }
        return nodes;
    }

    if (folder == "triggers") {
        const std::string sql = "SELECT name FROM " + MasterTable(database) +
                                " WHERE type = 'trigger' AND tbl_name = " +
                                db::QuoteStringLiteral(table) + " ORDER BY name";
        auto rows = dbutil::RunRows(connection, sql);
        RequireOk(rows);
        for (const auto& row : rows.rows) {
            nodes.push_back(MakeNode("trigger", RowText(row, 0), database, false, table));
        }
        return nodes;
    }

    return nodes;
}

json DetailForTable(db::Connection& connection, const std::string& database, const std::string& table) {
    json detail{{"database", database}, {"name", table}};
    json columns = json::array();
    json primary_key = json::array();

    auto column_rows = dbutil::RunRows(connection, Pragma(database, "table_xinfo", table));
    RequireOk(column_rows);
    for (const auto& row : column_rows.rows) {
        const std::int64_t hidden = RowInt(row, 6, 0);
        if (hidden == 1) continue;
        const std::string name = RowText(row, 1);
        const std::string type = RowText(row, 2);
        const std::int64_t pk = RowInt(row, 5, 0);
        json column{{"name", name},
                    {"type", type},
                    {"kind", db::KindName(db::KindFromDeclType(type))},
                    {"notNull", RowInt(row, 3, 0) != 0},
                    {"default", RowText(row, 4).empty() ? json(nullptr) : json(RowText(row, 4))},
                    {"primaryKey", pk > 0},
                    {"hidden", hidden},
                    {"ordinal", RowInt(row, 0)}};
        columns.push_back(std::move(column));
        if (pk > 0) primary_key.push_back(name);
    }

    json indexes = json::array();
    auto index_list = dbutil::RunRows(connection, Pragma(database, "index_list", table));
    RequireOk(index_list);
    for (const auto& row : index_list.rows) {
        const std::string index_name = RowText(row, 1);
        json index{{"name", index_name}, {"unique", RowInt(row, 2, 0) != 0},
                   {"origin", RowText(row, 3)}, {"partial", RowInt(row, 4, 0) != 0}};
        json index_columns = json::array();
        auto info = dbutil::RunRows(connection, Pragma(database, "index_info", index_name));
        if (info.ok()) {
            for (const auto& info_row : info.rows) {
                index_columns.push_back(RowText(info_row, 2));
            }
        }
        index["columns"] = std::move(index_columns);
        indexes.push_back(std::move(index));
    }

    json foreign_keys = json::array();
    auto fk_rows = dbutil::RunRows(connection, Pragma(database, "foreign_key_list", table));
    RequireOk(fk_rows);
    for (const auto& row : fk_rows.rows) {
        foreign_keys.push_back(json{{"id", RowInt(row, 0)},
                                    {"seq", RowInt(row, 1)},
                                    {"table", RowText(row, 2)},
                                    {"from", RowText(row, 3)},
                                    {"to", RowText(row, 4)},
                                    {"onUpdate", RowText(row, 5)},
                                    {"onDelete", RowText(row, 6)},
                                    {"match", RowText(row, 7)}});
    }

    json triggers = json::array();
    auto trigger_rows = dbutil::RunRows(
        connection,
        "SELECT name, sql FROM " + MasterTable(database) + " WHERE type = 'trigger' AND tbl_name = " +
            db::QuoteStringLiteral(table) + " ORDER BY name");
    RequireOk(trigger_rows);
    for (const auto& row : trigger_rows.rows) {
        triggers.push_back(json{{"name", RowText(row, 0)}, {"sql", RowText(row, 1)}});
    }

    auto ddl_rows = dbutil::RunRows(
        connection,
        "SELECT sql FROM " + MasterTable(database) + " WHERE type IN ('table','view') AND name = " +
            db::QuoteStringLiteral(table));
    std::string ddl;
    if (ddl_rows.ok() && !ddl_rows.rows.empty()) ddl = RowText(ddl_rows.rows.front(), 0);

    detail["columns"] = std::move(columns);
    detail["primaryKey"] = std::move(primary_key);
    detail["indexes"] = std::move(indexes);
    detail["foreignKeys"] = std::move(foreign_keys);
    detail["triggers"] = std::move(triggers);
    detail["sql"] = ddl;
    return detail;
}

json DdlForObject(db::Connection& connection,
                  const std::string& database,
                  const std::string& kind,
                  const std::string& name) {
    std::string type;
    if (kind == "table") {
        type = "table";
    } else if (kind == "view") {
        type = "view";
    } else if (kind == "index") {
        type = "index";
    } else if (kind == "trigger") {
        type = "trigger";
    } else {
        dbutil::ThrowCommandError("bad_args", "该对象没有 DDL: " + kind);
    }

    auto rows = dbutil::RunRows(
        connection,
        "SELECT sql FROM " + MasterTable(database) + " WHERE type = ? AND name = ?",
        {db::Param{db::ValueKind::Text, false, type}, db::Param{db::ValueKind::Text, false, name}});
    RequireOk(rows);
    if (rows.rows.empty()) {
        dbutil::ThrowCommandError("not_found", "对象不存在或没有 SQL 定义: " + name);
    }
    return json{{"sql", RowText(rows.rows.front(), 0)}, {"warnings", json::array()}};
}

}  // namespace

void RegisterMetadataCommands(AsyncRouter& router, session::SessionManager& sessions) {
    router.Register("meta.children", [&sessions](const json& args, TaskContext&) -> json {
        auto session = dbutil::RequireSession(sessions, args.value("sessionId", std::string{}));
        const json parent = args.value("parent", json::object());
        const std::string kind = parent.value("kind", std::string{"connection"});
        const std::string database = parent.value("database", std::string{"main"});
        const std::string name = parent.value("name", std::string{});
        const std::string table = parent.value("table", std::string{});

        return session->RunSync([&]() -> json {
            db::Connection& connection = session->Conn();
            json nodes = json::array();

            if (kind == "connection" || kind.empty()) {
                nodes = DatabaseChildren(connection);
            } else if (kind == "database") {
                const auto counts = CountObjects(connection, database);
                auto count_for = [&counts](const char* key) -> std::string {
                    const auto it = counts.find(key);
                    return it == counts.end() || it->second == 0 ? std::string{}
                                                                 : std::to_string(it->second);
                };
                nodes.push_back(FolderNode("tables", database, count_for("table")));
                nodes.push_back(FolderNode("views", database, count_for("view")));
                nodes.push_back(FolderNode("indexes", database, count_for("index")));
                nodes.push_back(FolderNode("triggers", database, count_for("trigger")));
            } else if (kind == "folder") {
                if (name == "columns" || name == "indexes" || name == "foreign_keys" ||
                    name == "triggers") {
                    if (!table.empty()) {
                        nodes = TableFolderChildren(connection, database, table, name);
                    } else {
                        nodes = FolderChildren(connection, database, name);
                    }
                } else {
                    nodes = FolderChildren(connection, database, name);
                }
            } else if (kind == "table" || kind == "view") {
                nodes = TableChildren(connection, database, name, kind == "view");
            }

            return json{{"nodes", nodes}, {"epoch", session->MetadataEpoch()}};
        });
    });

    router.Register("meta.search", [&sessions](const json& args, TaskContext&) -> json {
        auto session = dbutil::RequireSession(sessions, args.value("sessionId", std::string{}));
        const std::string keyword = args.value("keyword", std::string{});
        const int requested_limit = std::clamp(args.value("limit", 200), 1, 1000);

        std::vector<std::string> kinds;
        if (args.contains("kinds") && args["kinds"].is_array()) {
            for (const auto& item : args["kinds"]) {
                if (item.is_string()) kinds.push_back(item.get<std::string>());
            }
        }

        return session->RunSync([&]() -> json {
            json nodes = json::array();
            const std::uint64_t epoch = session->MetadataEpoch();
            if (keyword.empty()) {
                return json{{"nodes", nodes}, {"total", 0}, {"epoch", epoch}};
            }

            auto databases = dbutil::RunRows(session->Conn(), "PRAGMA database_list");
            RequireOk(databases);
            std::size_t remaining = static_cast<std::size_t>(requested_limit);

            for (const auto& db_row : databases.rows) {
                if (remaining == 0) break;
                if (db_row.size() < 2) continue;
                const std::string database = dbutil::CellText(db_row[1]);

                // instr + 参数化，避免 LIKE 通配符语义与用户输入注入。
                const std::string sql =
                    "SELECT name, type FROM " + MasterTable(database) +
                    " WHERE name NOT LIKE 'sqlite_%' AND instr(lower(name), lower(?)) > 0" +
                    " ORDER BY name LIMIT ?";
                auto rows = dbutil::RunRows(
                    session->Conn(), sql,
                    {db::Param{db::ValueKind::Text, false, keyword},
                     db::Param{db::ValueKind::Int, false, std::to_string(remaining)}});
                RequireOk(rows);

                for (const auto& row : rows.rows) {
                    if (row.size() < 2) continue;
                    const std::string name = dbutil::CellText(row[0]);
                    const std::string kind = dbutil::CellText(row[1]);
                    if (!kinds.empty() && std::find(kinds.begin(), kinds.end(), kind) == kinds.end()) {
                        continue;
                    }
                    const bool expandable = kind == "table" || kind == "view";
                    nodes.push_back(MakeNode(kind, name, database, expandable, kind));
                    --remaining;
                    if (remaining == 0) break;
                }
            }
            return json{{"nodes", nodes}, {"total", nodes.size()}, {"epoch", epoch}};
        });
    });

    router.Register("meta.detail", [&sessions](const json& args, TaskContext&) -> json {
        auto session = dbutil::RequireSession(sessions, args.value("sessionId", std::string{}));
        const json ref = args.value("ref", json::object());
        const std::string kind = ref.value("kind", std::string{"table"});
        const std::string database = ref.value("database", std::string{"main"});
        const std::string name = ref.value("name", std::string{});
        if (name.empty()) dbutil::ThrowCommandError("bad_args", "缺少对象名称");

        return session->RunSync([&]() -> json {
            if (kind == "table" || kind == "view") {
                return DetailForTable(session->Conn(), database, name);
            }
            return DdlForObject(session->Conn(), database, kind, name);
        });
    });

    router.Register("meta.ddl", [&sessions](const json& args, TaskContext&) -> json {
        auto session = dbutil::RequireSession(sessions, args.value("sessionId", std::string{}));
        const json ref = args.value("ref", json::object());
        const std::string kind = ref.value("kind", std::string{"table"});
        const std::string database = ref.value("database", std::string{"main"});
        const std::string name = ref.value("name", std::string{});
        if (name.empty()) dbutil::ThrowCommandError("bad_args", "缺少对象名称");

        return session->RunSync(
            [&]() -> json { return DdlForObject(session->Conn(), database, kind, name); });
    });

    router.Register("meta.refresh", [&sessions](const json& args, TaskContext&) -> json {
        auto session = dbutil::RequireSession(sessions, args.value("sessionId", std::string{}));
        // 后端元数据始终实时查询；这里推进代际，让前端丢弃本地缓存并重新拉取。
        return json{{"ok", true}, {"epoch", session->InvalidateMetadata()}};
    });
}

}  // namespace silkit::ipc
