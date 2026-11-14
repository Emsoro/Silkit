#include "ipc/commands_table.h"

#include "db/engine_dialect.h"
#include "db/identifier.h"
#include "db/session.h"
#include "db/table_ops.h"
#include "db/types.h"
#include "ipc/commands_db_common.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace silkit::ipc {
namespace {

using dbutil::json;

struct RowsResult {
    db::ExecResult result;
    std::vector<std::vector<db::Cell>> rows;
    bool ok() const { return result.error.code == 0; }
};

RowsResult RunRows(db::Connection& connection, const std::string& sql,
                   const std::vector<db::Param>& params, std::size_t max_cell_bytes) {
    RowsResult out;
    db::ExecOptions options;
    options.fetch_size = 1000;
    options.max_rows = 0;
    options.max_cell_bytes = max_cell_bytes;
    out.result = connection.Execute(sql, params, options, [&out](const db::RowBatch& batch) {
        for (const auto& row : batch.rows) out.rows.push_back(row);
        return true;
    });
    return out;
}

std::string NodeText(const std::vector<db::Cell>& row, std::size_t index) {
    if (index >= row.size() || row[index].is_null) return {};
    return row[index].text;
}

std::int64_t NodeInt(const std::vector<db::Cell>& row, std::size_t index, std::int64_t fallback = 0) {
    if (index >= row.size() || row[index].is_null) return fallback;
    char* end = nullptr;
    const long long value = std::strtoll(row[index].text.c_str(), &end, 10);
    return end != nullptr && *end == '\0' ? value : fallback;
}

db::TableRef RefFromJson(const json& ref) {
    db::TableRef out;
    out.kind = ref.value("kind", std::string{"table"});
    out.name = ref.value("name", std::string{});
    out.database = ref.value("database", std::string{});
    out.schema = ref.value("schema", std::string{});
    if (out.name.empty()) dbutil::ThrowCommandError("bad_args", "缺少表名");
    return out;
}

db::TableValue ValueFromJson(const json& value) {
    db::TableValue out;
    if (value.is_null()) {
        out.is_null = true;
        return out;
    }
    if (value.is_string()) {
        out.text = value.get<std::string>();
        return out;
    }
    if (value.is_boolean()) {
        out.text = value.get<bool>() ? "1" : "0";
        return out;
    }
    if (value.is_number()) {
        out.text = value.dump();
        return out;
    }
    out.text = value.dump();
    return out;
}

std::map<std::string, db::TableValue> ValuesFromJson(const json& object) {
    std::map<std::string, db::TableValue> out;
    if (!object.is_object()) return out;
    for (auto it = object.begin(); it != object.end(); ++it) {
        out[it.key()] = ValueFromJson(it.value());
    }
    return out;
}

db::TableChanges ChangesFromJson(const json& changes) {
    db::TableChanges out;
    if (changes.contains("inserts") && changes["inserts"].is_array()) {
        for (const auto& item : changes["inserts"]) {
            db::InsertSpec spec;
            spec.values = ValuesFromJson(item.value("values", json::object()));
            spec.client_index = static_cast<std::size_t>(std::max(0, item.value("clientIndex", 0)));
            out.inserts.push_back(std::move(spec));
        }
    }
    if (changes.contains("updates") && changes["updates"].is_array()) {
        for (const auto& item : changes["updates"]) {
            db::UpdateSpec spec;
            spec.keys = ValuesFromJson(item.value("keys", json::object()));
            spec.changes = ValuesFromJson(item.value("changes", json::object()));
            spec.client_index = static_cast<std::size_t>(std::max(0, item.value("clientIndex", 0)));
            out.updates.push_back(std::move(spec));
        }
    }
    if (changes.contains("deletes") && changes["deletes"].is_array()) {
        for (const auto& item : changes["deletes"]) {
            db::DeleteSpec spec;
            spec.keys = ValuesFromJson(item.value("keys", json::object()));
            spec.client_index = static_cast<std::size_t>(std::max(0, item.value("clientIndex", 0)));
            out.deletes.push_back(std::move(spec));
        }
    }
    return out;
}

std::vector<db::TableFilter> FiltersFromJson(const json& filters) {
    std::vector<db::TableFilter> out;
    if (!filters.is_array()) return out;
    for (const auto& item : filters) {
        if (!item.is_object()) continue;
        db::TableFilter filter;
        filter.column = item.value("column", std::string{});
        filter.op = item.value("op", std::string{"="});
        if (filter.column.empty()) continue;
        if (item.contains("value")) {
            const auto& value = item["value"];
            if (value.is_null()) {
                filter.value_is_null = true;
            } else if (value.is_array()) {
                for (const auto& part : value) {
                    if (part.is_string()) filter.values.push_back(part.get<std::string>());
                    else if (!part.is_null()) filter.values.push_back(part.dump());
                }
            } else if (value.is_string()) {
                filter.value = value.get<std::string>();
            } else {
                filter.value = value.dump();
            }
        }
        if (item.contains("value2") && !item["value2"].is_null()) {
            filter.value2 = item["value2"].is_string() ? item["value2"].get<std::string>()
                                                       : item["value2"].dump();
        }
        out.push_back(std::move(filter));
    }
    return out;
}

db::RowsQuery RowsQueryFromArgs(const json& args, const Settings& settings) {
    db::RowsQuery query;
    query.page = std::max(0, args.value("page", 0));
    const json snapshot = settings.Snapshot();
    int page_size = args.value("pageSize", 0);
    if (page_size <= 0 && snapshot.contains("data") && snapshot["data"].is_object()) {
        page_size = snapshot["data"].value("pageSize", 500);
    }
    query.page_size = std::clamp(page_size <= 0 ? 500 : page_size, 1, 2000);
    if (args.contains("order") && args["order"].is_object()) {
        const auto& order = args["order"];
        query.order.column = order.value("column", std::string{});
        query.order.desc = order.value("desc", false);
    }
    if (args.contains("filters")) query.filters = FiltersFromJson(args["filters"]);
    query.search = args.value("search", std::string{});
    return query;
}

json ColumnJson(const db::TableColumn& column) {
    const auto& meta = column.meta;
    return json{{"name", meta.name},
                {"type", meta.db_type},
                {"kind", db::KindName(meta.kind)},
                {"nullable", meta.nullable},
                {"primaryKey", meta.primary_key},
                {"autoIncrement", meta.auto_increment},
                {"generated", meta.generated},
                {"default", meta.default_expr.empty() ? json(nullptr) : json(meta.default_expr)},
                {"comment", meta.comment}};
}

json LayoutJson(const db::TableLayout& layout) {
    json columns = json::array();
    for (const auto& column : layout.columns) columns.push_back(ColumnJson(column));

    json identity{{"type", layout.identity.type}, {"columns", json::array()}};
    for (const auto& name : layout.identity.columns) identity["columns"].push_back(name);
    if (!layout.identity.index_name.empty()) identity["index"] = layout.identity.index_name;

    return json{{"columns", std::move(columns)},
                {"identity", std::move(identity)},
                {"editable", layout.editable},
                {"readOnlyReason", layout.read_only_reason}};
}

json ParamsJson(const std::vector<db::Param>& params) {
    json out = json::array();
    for (const auto& param : params) {
        if (param.is_null) out.push_back(nullptr);
        else out.push_back(param.text);
    }
    return out;
}

json StatementsJson(const std::vector<db::PreparedChange>& statements) {
    json out = json::array();
    for (const auto& statement : statements) {
        out.push_back(json{{"kind", statement.kind},
                           {"clientIndex", statement.client_index},
                           {"sql", statement.sql},
                           {"params", ParamsJson(statement.params)}});
    }
    return out;
}

json SqlPreviewJson(const std::vector<db::PreparedChange>& statements) {
    json out = json::array();
    for (const auto& statement : statements) out.push_back(statement.sql);
    return out;
}

[[noreturn]] void ThrowBuildError(const std::string& message) {
    dbutil::ThrowCommandError("bad_args", message);
}

[[noreturn]] void ThrowTableError(const db::DbError& error) {
    throw IpcError(dbutil::ErrorJson(error));
}

bool TryRollback(db::Connection& connection, const std::string& sql) {
    auto result = RunRows(connection, sql, {}, 0);
    return result.ok();
}

}  // namespace

void RegisterTableCommands(AsyncRouter& router,
                           session::SessionManager& sessions,
                           Settings& settings) {
    router.Register("table.rows", [&sessions, &settings](const json& args, TaskContext&) -> json {
        auto session = dbutil::RequireSession(sessions, args.value("sessionId", std::string{}));
        const db::TableRef ref = RefFromJson(args.value("ref", json::object()));
        const db::RowsQuery query = RowsQueryFromArgs(args, settings);
        const json snapshot = settings.Snapshot();
        std::size_t max_cell_bytes = 262144;
        if (snapshot.contains("data") && snapshot["data"].is_object()) {
            max_cell_bytes = static_cast<std::size_t>(
                std::max(0, snapshot["data"].value("maxCellBytes", 262144)));
        }

        return session->RunSync([&]() -> json {
            db::Connection& connection = session->Conn();
            db::TableLayout layout;
            db::DbError error;
            if (!db::LoadTableLayout(connection, ref, session->IsReadOnly(), layout, error)) {
                ThrowTableError(error);
            }

            db::BuiltQuery built;
            std::string build_error;
            if (!db::BuildRowsQuery(connection, layout, query, built, build_error)) {
                ThrowBuildError(build_error);
            }

            const auto started = std::chrono::steady_clock::now();
            auto rows = RunRows(connection, built.sql, built.params, max_cell_bytes);
            if (!rows.ok()) ThrowTableError(rows.result.error);
            const auto elapsed = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count());

            const std::size_t page_size = static_cast<std::size_t>(query.page_size);
            bool has_more = rows.rows.size() > page_size;
            if (has_more) rows.rows.resize(page_size);

            json output = LayoutJson(layout);
            json data_rows = json::array();
            json row_keys = json::array();
            const std::size_t offset = db::HasRowIdSelect(layout) ? 1 : 0;
            for (const auto& row : rows.rows) {
                json values = json::array();
                for (std::size_t i = offset; i < row.size(); ++i) {
                    values.push_back(dbutil::CellValueJson(row[i]));
                }
                data_rows.push_back(std::move(values));

                json keys = json::object();
                if (layout.identity.type == "rowid") {
                    keys["rowid"] = NodeText(row, 0);
                } else if (layout.identity.type == "pk" || layout.identity.type == "unique") {
                    for (const auto& name : layout.identity.columns) {
                        std::size_t index = 0;
                        bool found = false;
                        for (std::size_t i = 0; i < layout.columns.size(); ++i) {
                            if (layout.columns[i].meta.name == name) {
                                index = i;
                                found = true;
                                break;
                            }
                        }
                        keys[name] = found ? NodeText(row, offset + index) : std::string{};
                    }
                }
                row_keys.push_back(std::move(keys));
            }

            std::int64_t total = -1;
            std::string estimate_error;
            if (!db::EstimateTotalRows(connection, layout, query, total, estimate_error)) {
                total = -1;
            }

            json warnings = json::array();
            const std::int64_t offset_rows =
                static_cast<std::int64_t>(query.page) * static_cast<std::int64_t>(query.page_size);
            if (offset_rows >= 100000) {
                warnings.push_back("深分页（OFFSET " + std::to_string(offset_rows) +
                                   "）可能较慢，建议使用筛选缩小范围");
            }

            output["rows"] = std::move(data_rows);
            output["rowKeys"] = std::move(row_keys);
            output["page"] = query.page;
            output["pageSize"] = query.page_size;
            output["hasMore"] = has_more;
            output["totalEstimate"] = total >= 0 ? json(total) : json(nullptr);
            output["elapsedMs"] = elapsed;
            output["warnings"] = std::move(warnings);
            return output;
        });
    });

    router.Register("table.count", [&sessions, &settings](const json& args, TaskContext&) -> json {
        auto session = dbutil::RequireSession(sessions, args.value("sessionId", std::string{}));
        const db::TableRef ref = RefFromJson(args.value("ref", json::object()));
        const db::RowsQuery query = RowsQueryFromArgs(args, settings);

        return session->RunSync([&]() -> json {
            db::Connection& connection = session->Conn();
            db::TableLayout layout;
            db::DbError error;
            if (!db::LoadTableLayout(connection, ref, session->IsReadOnly(), layout, error)) {
                ThrowTableError(error);
            }
            db::BuiltQuery built;
            std::string build_error;
            if (!db::BuildCountQuery(connection, layout, query, built, build_error)) {
                ThrowBuildError(build_error);
            }
            const auto started = std::chrono::steady_clock::now();
            auto rows = RunRows(connection, built.sql, built.params, 0);
            if (!rows.ok()) ThrowTableError(rows.result.error);
            const auto elapsed = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count());
            return json{{"count", rows.rows.empty() ? 0 : NodeInt(rows.rows.front(), 0, 0)},
                        {"elapsedMs", elapsed}};
        });
    });

    router.Register("table.apply", [&sessions](const json& args, TaskContext&) -> json {
        auto session = dbutil::RequireSession(sessions, args.value("sessionId", std::string{}));
        const db::TableRef ref = RefFromJson(args.value("ref", json::object()));
        const bool dry_run = args.value("dryRun", false);
        const db::TableChanges changes =
            ChangesFromJson(args.value("changes", json::object()));

        return session->RunSync([&]() -> json {
            db::Connection& connection = session->Conn();
            db::TableLayout layout;
            db::DbError error;
            if (!db::LoadTableLayout(connection, ref, session->IsReadOnly(), layout, error)) {
                ThrowTableError(error);
            }
            if (!layout.editable) {
                dbutil::ThrowCommandError("read_only",
                                          layout.read_only_reason.empty() ? "该表只读"
                                                                          : layout.read_only_reason);
            }

            std::vector<db::PreparedChange> statements;
            std::string build_error;
            if (!db::BuildChanges(connection, layout, changes, statements, build_error)) {
                ThrowBuildError(build_error);
            }

            json preview = SqlPreviewJson(statements);
            json statements_json = StatementsJson(statements);
            if (dry_run || statements.empty()) {
                return json{{"dryRun", true},
                            {"sqlPreview", std::move(preview)},
                            {"statements", std::move(statements_json)},
                            {"affected", 0}};
            }

            const bool in_transaction = session->Tx() == session::TxState::InTransaction;
            const std::string begin_sql = in_transaction ? "SAVEPOINT silkit_apply" : "BEGIN";
            auto begin = RunRows(connection, begin_sql, {}, 0);
            if (!begin.ok()) ThrowTableError(begin.result.error);
            if (!in_transaction) session->SetTx(session::TxState::InTransaction);

            const auto started = std::chrono::steady_clock::now();
            std::uint64_t affected = 0;
            for (std::size_t i = 0; i < statements.size(); ++i) {
                const auto& statement = statements[i];
                auto result = RunRows(connection, statement.sql, statement.params, 0);
                if (!result.ok()) {
                    json payload = dbutil::ErrorJson(result.result.error);
                    payload["kind"] = statement.kind;
                    payload["clientIndex"] = statement.client_index;
                    payload["statementIndex"] = i;
                    payload["sql"] = statement.sql;
                    payload["params"] = ParamsJson(statement.params);
                    const std::string rollback_sql =
                        in_transaction ? "ROLLBACK TO SAVEPOINT silkit_apply" : "ROLLBACK";
                    if (!TryRollback(connection, rollback_sql)) {
                        payload["rollbackError"] = "回滚失败，请检查连接状态";
                    }
                    if (!in_transaction) session->SetTx(session::TxState::Autocommit);
                    payload["rolledBack"] = true;
                    throw IpcError(std::move(payload));
                }
                affected += result.result.affected;
            }

            const std::string finish_sql =
                in_transaction ? "RELEASE SAVEPOINT silkit_apply" : "COMMIT";
            auto finish = RunRows(connection, finish_sql, {}, 0);
            if (!finish.ok()) {
                json payload = dbutil::ErrorJson(finish.result.error);
                payload["rolledBack"] = false;
                if (!in_transaction) session->SetTx(session::TxState::Autocommit);
                throw IpcError(std::move(payload));
            }
            if (!in_transaction) session->SetTx(session::TxState::Autocommit);

            const auto elapsed = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count());
            json output{{"dryRun", false},
                        {"sqlPreview", std::move(preview)},
                        {"statements", std::move(statements_json)},
                        {"affected", affected},
                        {"applied", statements.size()},
                        {"elapsedMs", elapsed},
                        {"inTransaction", in_transaction}};
            if (in_transaction) {
                output["warning"] = "连接已在事务中，本次修改通过 SAVEPOINT 保存，未提交整个事务";
            }
            return output;
        });
    });

    router.Register("table.cell", [&sessions](const json& args, TaskContext&) -> json {
        auto session = dbutil::RequireSession(sessions, args.value("sessionId", std::string{}));
        const db::TableRef ref = RefFromJson(args.value("ref", json::object()));
        const std::string column_name = args.value("column", std::string{});
        const auto keys = ValuesFromJson(args.value("keys", json::object()));
        if (column_name.empty()) dbutil::ThrowCommandError("bad_args", "缺少列名");

        return session->RunSync([&]() -> json {
            db::Connection& connection = session->Conn();
            db::TableLayout layout;
            db::DbError error;
            if (!db::LoadTableLayout(connection, ref, session->IsReadOnly(), layout, error)) {
                ThrowTableError(error);
            }
            const db::TableColumn* column = db::FindColumn(layout, column_name);
            if (column == nullptr) dbutil::ThrowCommandError("bad_args", "列不存在: " + column_name);

            std::string where;
            std::vector<db::Param> params;
            std::string where_error;
            if (!db::BuildIdentityWhere(connection, layout, keys, where, params, where_error)) {
                ThrowBuildError(where_error);
            }
            const std::string sql = "SELECT " + db::QuoteIdentifier(layout.engine, column->meta.name) +
                                    " FROM " + layout.qualified + " WHERE " + where + " LIMIT 1";
            auto rows = RunRows(connection, sql, params, 0);
            if (!rows.ok()) ThrowTableError(rows.result.error);
            if (rows.rows.empty() || rows.rows.front().empty()) {
                dbutil::ThrowCommandError("not_found", "该行已不存在或被其他会话修改");
            }
            const auto& cell = rows.rows.front().front();
            return json{{"kind", db::KindName(cell.kind)},
                        {"text", cell.text},
                        {"bytes", cell.bytes},
                        {"truncated", cell.truncated},
                        {"isNull", cell.is_null},
                        {"column", column->meta.name},
                        {"columnType", column->meta.db_type}};
        });
    });

    router.Register("table.truncate", [&sessions](const json& args, TaskContext&) -> json {
        auto session = dbutil::RequireSession(sessions, args.value("sessionId", std::string{}));
        const db::TableRef ref = RefFromJson(args.value("ref", json::object()));
        const std::string confirm = args.value("confirmName", std::string{});
        if (confirm != ref.name) {
            dbutil::ThrowCommandError("bad_args", "请输入完整表名确认清空操作");
        }

        return session->RunSync([&]() -> json {
            db::Connection& connection = session->Conn();
            if (session->IsReadOnly()) {
                dbutil::ThrowCommandError("read_only", "只读连接不允许清空表");
            }
            db::TableLayout layout;
            db::DbError error;
            if (!db::LoadTableLayout(connection, ref, session->IsReadOnly(), layout, error)) {
                ThrowTableError(error);
            }
            if (layout.ref.kind != "table") dbutil::ThrowCommandError("bad_args", "视图不能清空");

            const std::string sql = layout.engine == db::Engine::Sqlite
                                        ? "DELETE FROM " + layout.qualified
                                        : "TRUNCATE TABLE " + layout.qualified;
            auto result = RunRows(connection, sql, {}, 0);
            if (!result.ok()) ThrowTableError(result.result.error);
            return json{{"ok", true}, {"affected", result.result.affected}};
        });
    });
}

}  // namespace silkit::ipc
