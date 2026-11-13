#pragma once

#include "db/types.h"

#include <memory>
#include <string>
#include <vector>

namespace silkit::db {

struct Capabilities {
    bool databases = false;
    bool schemas = false;
    bool views = true;
    bool routines = false;
    bool triggers = true;
    bool events = false;
    bool sequences = false;
    bool transactional_ddl = false;
    bool explain = true;
    bool cancel = true;
    bool user_management = false;
    bool table_comment = false;
};

struct ConnectParams {
    Engine engine = Engine::Sqlite;
    std::string host;
    std::string port;
    std::string user;
    std::string password;
    std::string database;
    std::string schema;
    std::string file;
    std::string charset;
    std::string ssl_mode;
    std::string ssl_ca;
    std::string ssl_cert;
    std::string ssl_key;
    bool read_only = false;
    int connect_timeout_ms = 8000;
    std::string application_name = "Silkit";
    std::string init_sql;

    // SQLite 专用
    int busy_timeout_ms = 5000;
    bool foreign_keys = true;
    std::string journal_mode;
};

class Connection {
public:
    virtual ~Connection() = default;

    virtual Engine EngineKind() const = 0;
    virtual Capabilities Caps() const = 0;

    virtual bool Ping(DbError& err) = 0;

    /// 只允许在 Session 线程调用。
    /// 一个 ExecResult 对应一条语句；多语句由 sql_splitter 拆分后顺序调用。
    virtual ExecResult Execute(const std::string& sql,
                               const std::vector<Param>& params,
                               const ExecOptions& opt,
                               const RowSink& sink) = 0;

    /// 必须线程安全，可从任意线程调用。
    virtual void Cancel() = 0;

    /// 运行时切换写语句拦截；只允许在 Session 线程调用。
    /// 底层句柄若是只读打开的，则不允许再切回可写（由 Session 判定）。
    virtual void SetReadOnly(bool read_only) = 0;
    virtual bool IsReadOnly() const = 0;

    virtual std::string ServerVersion() const = 0;
    virtual std::string CurrentDatabase() const = 0;
    virtual std::uint64_t ThreadId() const = 0;
    virtual std::string Placeholder(std::size_t index) const = 0;
};

class Driver {
public:
    virtual ~Driver() = default;
    virtual Engine EngineKind() const = 0;
    virtual std::unique_ptr<Connection> Open(const ConnectParams& params, DbError& err) = 0;
};

}  // namespace silkit::db