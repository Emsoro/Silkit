#include "db/session_manager.h"

#include "core/logger.h"
#include "db/driver_registry.h"

#include <utility>
#include <vector>

namespace silkit::session {
namespace {

nlohmann::json ReadSessionInfo(const std::shared_ptr<Session>& session) {
    return session->RunSync([&session]() -> nlohmann::json {
        const db::Capabilities caps = session->Conn().Caps();
        return nlohmann::json{
            {"version", session->Conn().ServerVersion()},
            {"database", session->Conn().CurrentDatabase()},
            {"capabilities",
             {{"databases", caps.databases},
              {"schemas", caps.schemas},
              {"views", caps.views},
              {"routines", caps.routines},
              {"triggers", caps.triggers},
              {"events", caps.events},
              {"sequences", caps.sequences},
              {"transactionalDdl", caps.transactional_ddl},
              {"explain", caps.explain},
              {"cancel", caps.cancel},
              {"userManagement", caps.user_management},
              {"tableComment", caps.table_comment}}}};
    });
}

void FillOutcome(const nlohmann::json& info, const db::ConnectParams& params,
                 OpenOutcome& outcome) {
    outcome.server_version = info.value("version", std::string{});
    outcome.current_database = info.value("database", params.database);
    const auto caps = info.value("capabilities", nlohmann::json::object());
    outcome.capabilities.databases = caps.value("databases", false);
    outcome.capabilities.schemas = caps.value("schemas", false);
    outcome.capabilities.views = caps.value("views", false);
    outcome.capabilities.routines = caps.value("routines", false);
    outcome.capabilities.triggers = caps.value("triggers", false);
    outcome.capabilities.events = caps.value("events", false);
    outcome.capabilities.sequences = caps.value("sequences", false);
    outcome.capabilities.transactional_ddl = caps.value("transactionalDdl", false);
    outcome.capabilities.explain = caps.value("explain", false);
    outcome.capabilities.cancel = caps.value("cancel", false);
    outcome.capabilities.user_management = caps.value("userManagement", false);
    outcome.capabilities.table_comment = caps.value("tableComment", false);
}

}  // namespace

SessionManager::SessionManager() { db::RegisterBuiltinDrivers(); }

SessionManager::~SessionManager() { Shutdown(); }

bool SessionManager::Open(const db::ConnectParams& params, OpenOutcome& outcome, db::DbError& error) {
    db::Driver* driver = db::DriverRegistry::Instance().Find(params.engine);
    if (driver == nullptr) {
        error.code = -1;
        error.message = std::string("未注册的数据库引擎: ") + db::EngineName(params.engine);
        return false;
    }

    auto connection = driver->Open(params, error);
    if (!connection) return false;

    std::string session_id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++next_id_;
        session_id = "s" + std::to_string(next_id_);
        auto session = std::make_shared<Session>(
            session_id, std::shared_ptr<db::Connection>(std::move(connection)), params);
        sessions_.emplace(session_id, std::move(session));
    }

    auto session = Find(session_id);
    if (!session) {
        error.code = -1;
        error.message = "创建会话失败";
        return false;
    }

    const nlohmann::json info = ReadSessionInfo(session);
    outcome.session_id = session_id;
    FillOutcome(info, params, outcome);

    LogInfo("session: 打开 " + session_id + " (" + db::EngineName(params.engine) + ")");
    return true;
}

bool SessionManager::Reconnect(const std::string& session_id, const db::ConnectParams& params,
                               OpenOutcome& outcome, db::DbError& error) {
    auto previous = Find(session_id);
    if (!previous) {
        error.code = -1;
        error.message = "会话不存在: " + session_id;
        return false;
    }

    db::Driver* driver = db::DriverRegistry::Instance().Find(params.engine);
    if (driver == nullptr) {
        error.code = -1;
        error.message = std::string("未注册的数据库引擎: ") + db::EngineName(params.engine);
        return false;
    }

    // 先在旧会话之外建立新连接；失败时旧会话完全不受影响。
    auto connection = driver->Open(params, error);
    if (!connection) return false;

    auto replacement = std::make_shared<Session>(
        session_id, std::shared_ptr<db::Connection>(std::move(connection)), params);
    const nlohmann::json info = ReadSessionInfo(replacement);

    bool replaced = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            previous = it->second;
            it->second = replacement;
            replaced = true;
        }
    }
    if (!replaced) {
        replacement->Shutdown();
        error.code = -1;
        error.message = "会话在重连期间被关闭";
        return false;
    }

    previous->Cancel();
    previous->Shutdown();

    outcome.session_id = session_id;
    FillOutcome(info, params, outcome);
    LogInfo("session: 重连 " + session_id + " (" + db::EngineName(params.engine) + ")");
    return true;
}

std::shared_ptr<Session> SessionManager::Find(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = sessions_.find(session_id);
    return it == sessions_.end() ? nullptr : it->second;
}

bool SessionManager::Close(const std::string& session_id) {
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return false;
        session = it->second;
        sessions_.erase(it);
    }

    session->Cancel();
    session->Shutdown();
    LogInfo("session: 关闭 " + session_id);
    return true;
}

void SessionManager::Shutdown() {
    std::vector<std::shared_ptr<Session>> sessions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : sessions_) sessions.push_back(entry.second);
        sessions_.clear();
    }
    for (auto& session : sessions) {
        session->Cancel();
        session->Shutdown();
    }
}

std::size_t SessionManager::Count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

}  // namespace silkit::session
