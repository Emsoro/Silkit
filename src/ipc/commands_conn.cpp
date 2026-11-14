#include "ipc/commands_conn.h"

#include "core/logger.h"
#include "core/secrets.h"
#include "db/driver_registry.h"
#include "db/engine_dialect.h"
#include "db/identifier.h"
#include "db/session.h"
#include "ipc/commands_db_common.h"
#include "ipc/commands_meta_engine.h"

#include <algorithm>
#include <chrono>
#include <string>

namespace silkit::ipc {
namespace {

using dbutil::json;

std::string PasswordFromProfile(const json& profile, const std::string& explicit_password) {
    if (!explicit_password.empty()) return explicit_password;
    const auto secret = profile.value("secret", json::object());
    const std::string stored = secret.value("password", std::string{});
    if (stored.empty()) return {};
    std::string error;
    const std::string plain = DecodeStoredSecret(stored, error);
    if (!plain.empty()) return plain;
    if (!error.empty()) LogWarn("conn: 密码解密失败: " + error);
    return {};
}

json ResolveProfile(const json& args, ProfileStore& profiles) {
    if (args.contains("profile") && args["profile"].is_object()) {
        json profile = args["profile"];
        // 前端拿到的 profile 已剥离 secret；未显式给密码时补上服务端保存的那份。
        const std::string id = profile.value("id", std::string{});
        if (!id.empty() && !profile.contains("secret")) {
            const json stored = profiles.Get(id);
            if (stored.is_object() && stored.contains("secret")) {
                profile["secret"] = stored["secret"];
            }
        }
        return profile;
    }
    const std::string id = args.value("id", std::string{});
    if (id.empty()) dbutil::ThrowCommandError("bad_args", "缺少连接 id 或 profile");
    const json profile = profiles.Get(id);
    if (profile.is_null()) dbutil::ThrowCommandError("profile_not_found", "连接不存在: " + id);
    return profile;
}

}  // namespace

void RegisterConnectionCommands(AsyncRouter& router,
                                ProfileStore& profiles,
                                session::SessionManager& sessions) {
    router.Register("conn.list", [&profiles](const json&, TaskContext&) -> json {
        return json{{"connections", profiles.List()}};
    });

    router.Register("conn.save", [&profiles](const json& args, TaskContext&) -> json {
        const json profile = args.value("profile", json::object());
        json stored = profile;

        const std::string password = args.value("password", std::string{});
        const bool save_password = args.value("savePassword", false);
        if (save_password && !password.empty()) {
            std::string protected_text;
            std::string error;
            if (!ProtectSecret(password, protected_text, error)) {
                dbutil::ThrowCommandError("secret_error", error);
            }
            stored["secret"] = json{{"save", true}, {"password", protected_text}};
        }

        std::string id;
        std::string error;
        if (!profiles.Upsert(stored, id, error)) {
            dbutil::ThrowCommandError("save_failed", error);
        }
        return json{{"id", id}};
    });

    router.Register("conn.delete", [&profiles](const json& args, TaskContext&) -> json {
        const std::string id = args.value("id", std::string{});
        std::string error;
        if (!profiles.Remove(id, error)) {
            dbutil::ThrowCommandError("delete_failed", error);
        }
        return json{{"ok", true}};
    });

    router.Register("conn.test", [&profiles](const json& args, TaskContext&) -> json {
        json profile = ResolveProfile(args, profiles);

        std::string error;
        db::ConnectParams params = dbutil::ParamsFromProfile(profile, PasswordFromProfile(profile, args.value("password", std::string{})), error);
        if (!error.empty()) dbutil::ThrowCommandError("bad_args", error);

        db::Driver* driver = db::DriverRegistry::Instance().Find(params.engine);
        if (driver == nullptr) dbutil::ThrowCommandError("engine_unavailable", "数据库驱动不可用");

        const auto started = std::chrono::steady_clock::now();
        db::DbError db_error;
        auto connection = driver->Open(params, db_error);
        if (!connection) {
            json payload = dbutil::ErrorJson(db_error);
            payload["elapsedMs"] = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count());
            throw IpcError(payload);
        }

        if (!connection->Ping(db_error)) {
            json payload = dbutil::ErrorJson(db_error);
            payload["elapsedMs"] = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count());
            throw IpcError(payload);
        }

        const auto elapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                  started)
                .count());
        return json{{"ok", true},
                    {"serverVersion", connection->ServerVersion()},
                    {"currentDb", connection->CurrentDatabase()},
                    {"elapsedMs", elapsed}};
    });

    router.Register("conn.open", [&profiles, &sessions](const json& args, TaskContext&) -> json {
        const json profile = ResolveProfile(args, profiles);
        const std::string explicit_password = args.value("password", std::string{});
        std::string error;
        db::ConnectParams params =
            dbutil::ParamsFromProfile(profile, PasswordFromProfile(profile, explicit_password), error);
        if (!error.empty()) dbutil::ThrowCommandError("bad_args", error);

        session::OpenOutcome outcome;
        db::DbError db_error;
        if (!sessions.Open(params, outcome, db_error)) {
            json payload = dbutil::ErrorJson(db_error);
            payload["message"] = "连接失败：" + db_error.message;
            throw IpcError(payload);
        }

        // 连接建立由驱动负责，取消钩子只用于已在途的查询，不在打开成功后注册。
        const std::string profile_id = profile.value("id", std::string{});
        if (!profile_id.empty()) profiles.Touch(profile_id);
        LogInfo("conn: 打开 " + profile.value("name", std::string{"未命名"}) + " -> " +
                outcome.session_id);

        json profile_ref = profile;
        profile_ref.erase("secret");
        profile_ref.erase("password");
        if (!profile_id.empty()) profile_ref["id"] = profile_id;
        return json{{"sessionId", outcome.session_id},
                    {"serverVersion", outcome.server_version},
                    {"currentDb", outcome.current_database},
                    {"capabilities", dbutil::CapabilitiesJson(outcome.capabilities)},
                    {"profile", profile_ref}};
    });

    router.Register("conn.close", [&sessions](const json& args, TaskContext& ctx) -> json {
        const std::string session_id = args.value("sessionId", std::string{});
        const bool closed = sessions.Close(session_id);
        if (closed && ctx.emitNamed) {
            ctx.emitNamed("session.closed",
                         json{{"sessionId", session_id}, {"reason", "closed"}});
        }
        return json{{"ok", closed}};
    });

    router.Register("conn.reconnect", [&profiles, &sessions](const json& args, TaskContext&) -> json {
        const std::string session_id = args.value("sessionId", std::string{});
        auto session = dbutil::RequireSession(sessions, session_id);

        db::ConnectParams params = session->Params();
        if (args.contains("profile") && args["profile"].is_object()) {
            const json profile = args["profile"];
            std::string error;
            params = dbutil::ParamsFromProfile(
                profile, PasswordFromProfile(profile, args.value("password", std::string{})), error);
            if (!error.empty()) dbutil::ThrowCommandError("bad_args", error);
        } else {
            const std::string password = args.value("password", std::string{});
            if (!password.empty()) params.password = password;
        }

        session::OpenOutcome outcome;
        db::DbError error;
        if (!sessions.Reconnect(session_id, params, outcome, error)) {
            json payload = dbutil::ErrorJson(error);
            payload["message"] = "重连失败：" + error.message;
            throw IpcError(std::move(payload));
        }

        const std::string profile_id = args.value("id", std::string{});
        if (!profile_id.empty()) profiles.Touch(profile_id);
        LogInfo("conn: 重连 " + session_id);
        return json{{"sessionId", outcome.session_id},
                    {"serverVersion", outcome.server_version},
                    {"currentDb", outcome.current_database},
                    {"capabilities", dbutil::CapabilitiesJson(outcome.capabilities)}};
    });

    router.Register("conn.ping", [&sessions](const json& args, TaskContext&) -> json {
        auto session = dbutil::RequireSession(sessions, args.value("sessionId", std::string{}));
        const auto started = std::chrono::steady_clock::now();
        const json result = session->RunSync([&session]() -> json {
            db::DbError error;
            const bool ok = session->Conn().Ping(error);
            return json{{"ok", ok}, {"error", dbutil::ErrorJson(error)}};
        });
        const auto elapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                  started)
                .count());
        return json{{"ok", result.value("ok", false)}, {"latencyMs", elapsed},
                    {"error", result.value("error", json::object())}};
    });

    router.Register("conn.stats", [&sessions](const json& args, TaskContext&) -> json {
        auto session = dbutil::RequireSession(sessions, args.value("sessionId", std::string{}));
        return session->RunSync([&session]() -> json {
            auto& conn = session->Conn();
            std::string charset;
            std::string timezone;
            if (IsRelationalEngine(conn.EngineKind())) {
                const auto stats = EngineServerStats(conn);
                charset = stats.first;
                timezone = stats.second;
            } else {
                auto encoding = dbutil::RunRows(conn, "PRAGMA encoding");
                if (encoding.ok() && !encoding.rows.empty() && !encoding.rows.front().empty()) {
                    charset = dbutil::CellText(encoding.rows.front().front());
                }
            }
            return json{{"threadId", conn.ThreadId()},
                        {"serverVersion", conn.ServerVersion()},
                        {"currentDb", session->CurrentDatabase()},
                        {"tx", session::TxStateName(session->Tx())},
                        {"readOnly", session->IsReadOnly()},
                        {"charset", charset},
                        {"timezone", timezone},
                        {"capabilities", dbutil::CapabilitiesJson(conn.Caps())}};
        });
    });

    router.Register("conn.set_database", [&sessions](const json& args, TaskContext&) -> json {
        auto session = dbutil::RequireSession(sessions, args.value("sessionId", std::string{}));
        const std::string target = args.value("database", std::string{});
        if (target.empty()) dbutil::ThrowCommandError("bad_args", "缺少 database");
        if (!db::IsSafeIdentifier(target)) {
            dbutil::ThrowCommandError("bad_args", "数据库名不合法");
        }

        // PostgreSQL 切换数据库必须新建连接；重连会关闭旧 Session，不能在 RunSync 内执行。
        if (session->Params().engine == db::Engine::Postgres) {
            db::ConnectParams params = session->Params();
            params.database = target;
            session::OpenOutcome outcome;
            db::DbError error;
            if (!sessions.Reconnect(session->Id(), params, outcome, error)) {
                throw IpcError(dbutil::ErrorJson(error));
            }
            return json{{"ok", true}, {"currentDb", target}};
        }

        return session->RunSync([&]() -> json {
            auto& conn = session->Conn();
            if (conn.EngineKind() == db::Engine::MySql) {
                auto rows = dbutil::RunRows(conn, db::SwitchDatabaseSql(db::Engine::MySql, target));
                if (!rows.ok()) throw IpcError(dbutil::ErrorJson(rows.result.error));
                session->SetCurrentDatabase(target);
                return json{{"ok", true}, {"currentDb", target}};
            }
            // SQLite 没有 USE，只能切换到已 ATTACH 的库；名字必须来自目录。
            auto rows = dbutil::RunRows(conn, "PRAGMA database_list");
            if (!rows.ok()) throw IpcError(dbutil::ErrorJson(rows.result.error));
            bool attached = false;
            for (const auto& row : rows.rows) {
                if (row.size() >= 2 && dbutil::CellText(row[1]) == target) {
                    attached = true;
                    break;
                }
            }
            if (!attached) {
                dbutil::ThrowCommandError("database_not_attached",
                                          "数据库未附加：" + target + "（可用 ATTACH DATABASE 添加）");
            }
            session->SetCurrentDatabase(target);
            return json{{"ok", true}, {"currentDb", target}};
        });
    });

    router.Register("conn.set_readonly", [&sessions](const json& args, TaskContext&) -> json {
        auto session = dbutil::RequireSession(sessions, args.value("sessionId", std::string{}));
        const bool read_only = args.value("readOnly", true);
        return session->RunSync([&]() -> json {
            std::string error;
            if (!session->SetReadOnly(read_only, error)) {
                dbutil::ThrowCommandError("read_only_conflict", error);
            }
            return json{{"ok", true}, {"readOnly", session->IsReadOnly()}};
        });
    });
}

}  // namespace silkit::ipc
