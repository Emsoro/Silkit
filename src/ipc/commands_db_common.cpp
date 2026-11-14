#include "ipc/commands_db_common.h"

#include "db/engine_dialect.h"
#include "db/identifier.h"
#include "db/sql_util.h"
#include "db/types.h"

#include <cstdlib>
#include <utility>

namespace silkit::ipc::dbutil {
namespace {

int IntValue(const json& object, const char* key, int fallback) {
    if (!object.is_object() || !object.contains(key)) return fallback;
    const auto& value = object[key];
    if (value.is_number_integer()) return value.get<int>();
    if (value.is_string()) {
        try {
            return std::stoi(value.get<std::string>());
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

bool BoolValue(const json& object, const char* key, bool fallback) {
    if (!object.is_object() || !object.contains(key)) return fallback;
    const auto& value = object[key];
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number_integer()) return value.get<int>() != 0;
    return fallback;
}

std::string StringValue(const json& object, const char* key) {
    if (!object.is_object() || !object.contains(key)) return {};
    const auto& value = object[key];
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number()) return value.dump();
    return {};
}

int NumericCode(const std::string& code) {
    if (code == "bad_args") return -32602;
    if (code == "unknown_command") return -32601;
    return -32000;
}

}  // namespace

db::ConnectParams ParamsFromProfile(const json& profile, const std::string& password, std::string& error) {
    db::ConnectParams params;
    if (!profile.is_object()) {
        error = "连接配置必须是对象";
        return params;
    }

    bool ok = false;
    params.engine = db::EngineFromName(profile.value("engine", std::string{"sqlite"}), ok);
    if (!ok) {
        error = "不支持的数据库引擎: " + profile.value("engine", std::string{});
        return params;
    }

    params.host = profile.value("host", std::string{"127.0.0.1"});
    params.port = profile.contains("port") && !profile["port"].is_null()
                      ? (profile["port"].is_string() ? profile["port"].get<std::string>()
                                                     : std::to_string(profile["port"].get<int>()))
                      : std::string{};
    params.user = profile.value("user", std::string{});
    params.database = profile.value("database", std::string{});
    params.schema = profile.value("schema", std::string{});
    params.file = profile.value("file", std::string{});
    params.charset = profile.value("charset", std::string{"utf8mb4"});
    params.password = password;
    if (params.password.empty()) {
        const auto secret = profile.value("secret", json::object());
        params.password = secret.value("password", std::string{});
    }

    const auto ssl = profile.value("ssl", json::object());
    params.ssl_mode = ssl.value("mode", std::string{"prefer"});
    params.ssl_ca = ssl.value("ca", std::string{});
    params.ssl_cert = ssl.value("cert", std::string{});
    params.ssl_key = ssl.value("key", std::string{});

    params.read_only = BoolValue(profile, "readOnly", false);
    const auto options = profile.value("options", json::object());
    params.connect_timeout_ms = IntValue(options, "connectTimeoutMs", 8000);
    params.application_name = options.value("applicationName", std::string{"Silkit"});
    params.init_sql = options.value("initSql", std::string{});
    params.busy_timeout_ms = IntValue(options, "busyTimeoutMs", 5000);
    params.foreign_keys = BoolValue(options, "foreignKeys", true);
    params.journal_mode = StringValue(options, "journalMode");

    if (params.engine == db::Engine::Sqlite) {
        if (params.file.empty()) error = "SQLite 文件路径不能为空";
    } else {
        if (params.host.empty()) params.host = "127.0.0.1";
        if (params.port.empty()) params.port = db::DefaultPort(params.engine);
        if (params.database.empty() && params.engine == db::Engine::Postgres) {
            params.database = db::DefaultDatabase(params.engine);
        }
        if (params.schema.empty() && params.engine == db::Engine::Postgres) {
            params.schema = "public";
        }
        if (params.user.empty()) error = "数据库用户名不能为空";
    }
    return params;
}

json CapabilitiesJson(const db::Capabilities& caps) {
    return json{{"databases", caps.databases}, {"schemas", caps.schemas},
                {"views", caps.views}, {"routines", caps.routines},
                {"triggers", caps.triggers}, {"events", caps.events},
                {"sequences", caps.sequences}, {"transactionalDdl", caps.transactional_ddl},
                {"explain", caps.explain}, {"cancel", caps.cancel},
                {"userManagement", caps.user_management}, {"tableComment", caps.table_comment}};
}

json ErrorJson(const db::DbError& error) {
    json out{{"code", error.code}, {"message", error.message}};
    if (!error.sql_state.empty()) out["sqlState"] = error.sql_state;
    if (!error.detail.empty()) out["detail"] = error.detail;
    if (!error.hint.empty()) out["hint"] = error.hint;
    if (error.position >= 0) out["position"] = error.position;
    return out;
}

json ColumnJson(const db::ColumnMeta& column) {
    return json{{"name", column.name},
                {"type", column.db_type},
                {"kind", db::KindName(column.kind)},
                {"nullable", column.nullable},
                {"primaryKey", column.primary_key},
                {"autoIncrement", column.auto_increment},
                {"default", column.default_expr},
                {"comment", column.comment}};
}

json CellValueJson(const db::Cell& cell) {
    if (cell.is_null) return nullptr;
    return cell.text;
}

void ThrowCommandError(const std::string& code, const std::string& message) {
    // 字符串 code 保持不变（前端已按字符串处理），另附 §8.4 约定的数字码。
    throw IpcError(json{{"code", code},
                        {"numericCode", NumericCode(code)},
                        {"message", message}});
}

std::shared_ptr<session::Session> RequireSession(const session::SessionManager& sessions,
                                                 const std::string& session_id) {
    auto found = sessions.Find(session_id);
    if (!found) {
        ThrowCommandError("session_not_found", "连接已关闭或不存在: " + session_id);
    }
    return found;
}

RowsResult RunRows(db::Connection& connection,
                   const std::string& sql,
                   const std::vector<db::Param>& params) {
    RowsResult out;
    db::ExecOptions options;
    options.fetch_size = 1000;
    options.max_rows = 0;
    options.max_cell_bytes = 0;

    out.result = connection.Execute(sql, params, options, [&out](const db::RowBatch& batch) {
        for (const auto& row : batch.rows) out.rows.push_back(row);
        return true;
    });
    return out;
}

std::string CellText(const db::Cell& cell) {
    return cell.is_null ? std::string{} : cell.text;
}

std::string NodeNameFromCell(const db::Cell& cell) { return CellText(cell); }

std::int64_t CellInt(const db::Cell& cell, std::int64_t fallback) {
    if (cell.is_null || cell.text.empty()) return fallback;
    char* end = nullptr;
    const long long value = std::strtoll(cell.text.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') return fallback;
    return value;
}

void CheckSafeMode(const json& args, const json& settings,
                   const std::vector<db::SqlStatement>& statements) {
    if (!settings.is_object()) return;
    const auto query_it = settings.find("query");
    if (query_it == settings.end() || !query_it->is_object()) return;
    if (!query_it->value("safeMode", true)) return;

    const auto hits = db::FindDestructiveStatements(statements);
    if (hits.empty()) return;

    const std::string token = std::to_string(db::DestructiveToken(statements));
    const bool confirmed = args.value("confirmDestructive", false) &&
                           args.value("confirmToken", std::string{}) == token;
    if (confirmed) return;

    json list = json::array();
    for (std::size_t i = 0; i < hits.size() && i < 20; ++i) {
        list.push_back(json{{"index", hits[i].index}, {"sql", hits[i].sql}});
    }
    throw IpcError(json{
        {"code", "confirm_required"},
        {"numericCode", NumericCode("confirm_required")},
        {"message", "安全模式：检测到 " + std::to_string(hits.size()) +
                        " 条危险语句（DROP / TRUNCATE / ALTER / 无 WHERE 的 UPDATE / DELETE）"},
        {"token", token},
        {"count", hits.size()},
        {"statements", std::move(list)}});
}

}  // namespace silkit::ipc::dbutil
