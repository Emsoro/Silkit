#pragma once

#include "db/driver.h"
#include "db/session.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace silkit::session {

struct OpenOutcome {
    std::string session_id;
    std::string server_version;
    std::string current_database;
    db::Capabilities capabilities;
};

/// 管理所有打开的数据库会话。线程安全。
class SessionManager {
public:
    SessionManager();
    ~SessionManager();

    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    bool Open(const db::ConnectParams& params, OpenOutcome& outcome, db::DbError& error);

    /// 用同一 sessionId 重建底层连接；打开失败时保留原会话，成功才替换。
    bool Reconnect(const std::string& session_id, const db::ConnectParams& params,
                   OpenOutcome& outcome, db::DbError& error);

    std::shared_ptr<Session> Find(const std::string& session_id) const;
    bool Close(const std::string& session_id);
    void Shutdown();

    std::size_t Count() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;
    std::uint64_t next_id_ = 0;
};

}  // namespace silkit::session
