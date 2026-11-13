#pragma once

#include "db/driver.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace silkit::session {

enum class TxState { Autocommit, InTransaction };

const char* TxStateName(TxState state);

/// 一个数据库连接对应一个 Session 线程：驱动对象只在该线程使用，
/// 事务/会话状态天然串行化。Cancel 可从任意线程调用（驱动保证线程安全）。
class Session {
public:
    Session(std::string id, std::shared_ptr<db::Connection> connection, db::ConnectParams params);
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    const std::string& Id() const { return id_; }

    /// 打开时使用的参数，构造后不再变化，重连时复用/覆盖。
    const db::ConnectParams& Params() const { return params_; }

    /// 只能在 Session 线程调用。
    db::Connection& Conn() { return *connection_; }

    /// 投递任务到 Session 线程；Session 已关闭时返回 false。
    bool Post(std::function<void()> job);

    /// 在 Session 线程执行并等待结果；异常会原样抛给调用方。
    nlohmann::json RunSync(std::function<nlohmann::json()> job);

    /// 线程安全：中断当前查询（sqlite3_interrupt / KILL QUERY / PQcancel）。
    void Cancel();

    /// 停止线程（排空队列后退出）。可重复调用。
    void Shutdown();

    bool Alive() const { return alive_.load(std::memory_order_acquire); }

    /// 会话级状态，可从任意线程读取（内部加锁）。
    std::string CurrentDatabase() const;
    void SetCurrentDatabase(std::string database);
    TxState Tx() const;
    void SetTx(TxState state);
    bool IsReadOnly() const;
    std::uint64_t MetadataEpoch() const;
    std::uint64_t InvalidateMetadata();

    /// 必须在 Session 线程调用（通常经 RunSync），同时同步驱动侧拦截。
    bool SetReadOnly(bool read_only, std::string& error);

private:
    void Loop();

    std::string id_;
    std::shared_ptr<db::Connection> connection_;
    db::ConnectParams params_;

    mutable std::mutex state_mutex_;
    std::string current_database_;
    TxState tx_ = TxState::Autocommit;
    bool read_only_ = false;
    std::uint64_t metadata_epoch_ = 0;

    mutable std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::queue<std::function<void()>> jobs_;
    bool stopping_ = false;

    std::mutex shutdown_mutex_;
    std::thread thread_;
    std::atomic<bool> alive_{true};
};

}  // namespace silkit::session
