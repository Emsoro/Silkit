#include "db/session.h"

#include "core/logger.h"

#include <future>
#include <stdexcept>
#include <utility>

namespace silkit::session {

const char* TxStateName(TxState state) {
    return state == TxState::InTransaction ? "in_transaction" : "autocommit";
}

Session::Session(std::string id, std::shared_ptr<db::Connection> connection, db::ConnectParams params)
    : id_(std::move(id)), connection_(std::move(connection)), params_(std::move(params)) {
    current_database_ = connection_->CurrentDatabase();
    read_only_ = params_.read_only;
    thread_ = std::thread([this] { Loop(); });
}

Session::~Session() { Shutdown(); }

std::string Session::CurrentDatabase() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_database_;
}

void Session::SetCurrentDatabase(std::string database) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    current_database_ = std::move(database);
}

TxState Session::Tx() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return tx_;
}

void Session::SetTx(TxState state) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    tx_ = state;
}

bool Session::IsReadOnly() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return read_only_;
}

std::uint64_t Session::MetadataEpoch() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return metadata_epoch_;
}

std::uint64_t Session::InvalidateMetadata() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return ++metadata_epoch_;
}

bool Session::SetReadOnly(bool read_only, std::string& error) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (params_.read_only && !read_only) {
            error = "连接以只读模式打开，无法恢复为可写；请重新打开连接";
            return false;
        }
        read_only_ = read_only;
    }
    // 驱动对象只允许在 Session 线程访问，调用方需通过 RunSync 进入。
    connection_->SetReadOnly(read_only);
    return true;
}

bool Session::Post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (stopping_) return false;
        jobs_.push(std::move(job));
    }
    cv_.notify_one();
    return true;
}

nlohmann::json Session::RunSync(std::function<nlohmann::json()> job) {
    if (!job) return nlohmann::json::object();

    // 防御：万一在 Session 线程上重入，直接执行，避免自我等待。
    if (std::this_thread::get_id() == thread_.get_id()) {
        return job();
    }

    auto promise = std::make_shared<std::promise<nlohmann::json>>();
    auto future = promise->get_future();

    const bool posted = Post([job = std::move(job), promise]() mutable {
        try {
            promise->set_value(job());
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    });

    if (!posted) {
        throw std::runtime_error("会话已关闭");
    }
    return future.get();
}

void Session::Cancel() {
    if (connection_) connection_->Cancel();
}

void Session::Shutdown() {
    std::lock_guard<std::mutex> shutdown_lock(shutdown_mutex_);
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stopping_ = true;
    }
    cv_.notify_all();

    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) {
        thread_.join();
    }
    alive_.store(false, std::memory_order_release);
}

void Session::Loop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (jobs_.empty()) {
                if (stopping_) break;
                continue;
            }
            job = std::move(jobs_.front());
            jobs_.pop();
        }

        try {
            job();
        } catch (const std::exception& e) {
            LogError("session: 任务异常 " + id_ + ": " + e.what());
        } catch (...) {
            LogError("session: 任务未知异常 " + id_);
        }
    }
    alive_.store(false, std::memory_order_release);
}

}  // namespace silkit::session
