#include "ipc/commands_query.h"

#include "db/engine_dialect.h"
#include "db/session.h"
#include "db/sql_splitter.h"
#include "db/sql_util.h"
#include "db/type_map.h"
#include "ipc/commands_db_common.h"

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace silkit::ipc {
namespace {

using dbutil::json;

json RowsJson(const std::vector<std::vector<db::Cell>>& rows) {
    json out = json::array();
    for (const auto& row : rows) {
        json values = json::array();
        for (const auto& cell : row) values.push_back(dbutil::CellValueJson(cell));
        out.push_back(std::move(values));
    }
    return out;
}

json ColumnsJson(const std::vector<db::ColumnMeta>& columns) {
    json out = json::array();
    for (const auto& column : columns) {
        out.push_back(json{{"name", column.name},
                           {"type", column.db_type},
                           {"kind", db::KindName(column.kind)},
                           {"nullable", column.nullable},
                           {"primaryKey", column.primary_key}});
    }
    return out;
}

std::size_t OptionSize(const json& args, const json& settings, const char* arg_key,
                       const char* section, const char* setting_key, std::size_t fallback) {
    if (args.contains(arg_key) && args[arg_key].is_number_integer()) {
        const auto value = args[arg_key].get<long long>();
        if (value >= 0) return static_cast<std::size_t>(value);
    }
    if (settings.contains(section) && settings[section].is_object() &&
        settings[section].contains(setting_key) &&
        settings[section][setting_key].is_number_integer()) {
        const auto value = settings[section][setting_key].get<long long>();
        if (value >= 0) return static_cast<std::size_t>(value);
    }
    return fallback;
}

}  // namespace

void RegisterQueryCommands(AsyncRouter& router,
                           session::SessionManager& sessions,
                           Settings& settings) {
    router.Register("query.execute", [&sessions, &settings](const json& args, TaskContext& ctx) -> json {
        auto session = dbutil::RequireSession(sessions, args.value("sessionId", std::string{}));
        const std::string sql = args.value("sql", std::string{});
        if (sql.empty()) dbutil::ThrowCommandError("bad_args", "SQL 不能为空");
        const bool continue_on_error = args.value("continueOnError", false);

        const json snapshot = settings.Snapshot();
        const std::size_t max_rows =
            OptionSize(args, snapshot, "maxRows", "query", "maxRows", 5000);
        const std::size_t fetch_size =
            OptionSize(args, snapshot, "fetchSize", "query", "fetchSize", 500);
        const std::size_t max_cell_bytes =
            OptionSize(args, snapshot, "maxCellBytes", "data", "maxCellBytes", 262144);

        ctx.SetCancelHook([session] { session->Cancel(); });

        try {
            return session->RunSync([&]() -> json {
                db::Connection& connection = session->Conn();
                const db::SplitOptions split_options =
                    db::SplitOptionsForEngine(connection.EngineKind() == db::Engine::MySql
                                                  ? 1
                                                  : (connection.EngineKind() == db::Engine::Postgres ? 2 : 0));
                const std::vector<db::SqlStatement> statements = db::SplitSql(sql, split_options);
                if (statements.empty()) dbutil::ThrowCommandError("empty_sql", "没有可执行的语句");
                dbutil::CheckSafeMode(args, snapshot, statements);

                const auto started = std::chrono::steady_clock::now();
                json summaries = json::array();
                json errors = json::array();
                std::size_t remaining = max_rows;

                for (std::size_t i = 0; i < statements.size(); ++i) {
                    if (ctx.IsCancelled()) return json{{"cancelled", true}};
                    if (max_rows > 0 && remaining == 0) break;

                    const auto& statement = statements[i];
                    ctx.emit("statement", json{{"index", i},
                                               {"total", statements.size()},
                                               {"offset", statement.begin_offset},
                                               {"sql", statement.text}});

                    db::ExecOptions options;
                    options.fetch_size = fetch_size == 0 ? 500 : fetch_size;
                    options.max_rows = max_rows == 0 ? 0 : remaining;
                    options.max_cell_bytes = max_cell_bytes;
                    std::size_t seq = 0;

                    const db::ExecResult result =
                        connection.Execute(statement.text, {}, options, [&](const db::RowBatch& batch) {
                            if (ctx.IsCancelled()) return false;
                            json data{{"statement", i}, {"seq", seq++}, {"rows", RowsJson(batch.rows)}};
                            if (!batch.columns.empty()) data["columns"] = ColumnsJson(batch.columns);
                            ctx.emit("batch", std::move(data));
                            return true;
                        });

                    if (ctx.IsCancelled() || result.cancelled) {
                        return json{{"cancelled", true}};
                    }

                    if (result.error.code != 0) {
                        json payload = dbutil::ErrorJson(result.error);
                        payload["statementIndex"] = i;
                        payload["sql"] = statement.text;
                        payload["offset"] =
                            statement.begin_offset + (result.error.position > 0
                                                          ? static_cast<std::size_t>(result.error.position - 1)
                                                          : 0);
                        payload["elapsedMs"] = result.elapsed_ms;
                        if (!continue_on_error) throw IpcError(std::move(payload));
                        errors.push_back(payload);
                        summaries.push_back(json{{"index", i},
                                                  {"rows", 0},
                                                  {"affected", 0},
                                                  {"elapsedMs", result.elapsed_ms},
                                                  {"truncated", false},
                                                  {"columns", 0},
                                                  {"cancelled", false},
                                                  {"error", payload}});
                        continue;
                    }

                    // 事务状态跟随已成功执行的语句，供状态栏与 conn.stats 展示。
                    const std::string keyword = db::FirstSqlKeyword(statement.text);
                    if (keyword == "BEGIN" || keyword == "START") {
                        session->SetTx(session::TxState::InTransaction);
                    } else if (keyword == "COMMIT" || keyword == "END" || keyword == "ROLLBACK") {
                        session->SetTx(session::TxState::Autocommit);
                    }

                    summaries.push_back(json{{"index", i},
                                             {"rows", result.rows},
                                             {"affected", result.affected},
                                             {"elapsedMs", result.elapsed_ms},
                                             {"truncated", result.truncated},
                                             {"columns", result.columns.size()},
                                             {"cancelled", result.cancelled}});

                    if (max_rows > 0) {
                        remaining = result.rows >= remaining ? 0 : remaining - result.rows;
                    }
                }

                const auto elapsed = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started)
                        .count());
                json response{{"statements", summaries}, {"elapsedMs", elapsed}};
                if (continue_on_error) response["errors"] = std::move(errors);
                return response;
            });
        } catch (...) {
            ctx.ClearCancelHook();
            throw;
        }
    });

    router.Register("query.cancel", [&sessions](const json& args, TaskContext&) -> json {
        const std::string session_id = args.value("sessionId", std::string{});
        if (auto session = sessions.Find(session_id)) {
            session->Cancel();
            return json{{"ok", true}};
        }
        return json{{"ok", false}};
    });

    router.Register("query.explain", [&sessions](const json& args, TaskContext&) -> json {
        auto session = dbutil::RequireSession(sessions, args.value("sessionId", std::string{}));
        const std::string sql = args.value("sql", std::string{});
        if (sql.empty()) dbutil::ThrowCommandError("bad_args", "SQL 不能为空");
        const bool analyze = args.value("analyze", false);

        return session->RunSync([&]() -> json {
            db::Connection& connection = session->Conn();
            const db::Engine engine = connection.EngineKind();
            const std::string explain = db::ExplainSql(engine, sql, analyze);
            auto rows = dbutil::RunRows(connection, explain);
            if (!rows.ok()) throw IpcError(dbutil::ErrorJson(rows.result.error));
            json warnings = json::array();
            if (analyze && engine == db::Engine::Sqlite) {
                warnings.push_back("SQLite 的 EXPLAIN QUERY PLAN 不支持 ANALYZE，已忽略该选项");
            }
            if (engine == db::Engine::MySql && analyze) {
                warnings.push_back("EXPLAIN ANALYZE 需要 MySQL 8.0.18+，旧版本可能报错");
            }
            if (engine == db::Engine::Postgres && analyze) {
                warnings.push_back("EXPLAIN ANALYZE 会真实执行语句，写语句会修改数据");
            }
            return json{{"format", "text"},
                        {"engine", engine == db::Engine::MySql ? "mysql"
                                   : (engine == db::Engine::Postgres ? "postgres" : "sqlite")},
                        {"plan", RowsJson(rows.rows)},
                        {"warnings", std::move(warnings)}};
        });
    });
}

}  // namespace silkit::ipc
