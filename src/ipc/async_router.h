#pragma once

#include <nlohmann/json.hpp>
#include <tauricpp/bridge.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace silkit::ipc {

using json = nlohmann::json;

/// 处理函数抛出此异常时，Router 以 `type=error` 结束请求，并原样透传 error 对象
/// （用于携带 statementIndex/position/sqlState 等结构化错误）。
class IpcError : public std::runtime_error {
public:
    explicit IpcError(json payload)
        : std::runtime_error(payload.value("message", std::string{"IPC 执行失败"})),
          error(std::move(payload)) {}

    json error;
};

/// 交给异步处理函数的上下文：进度回传、自定义事件、取消标记。
struct TaskContext {
    std::atomic<bool> cancelled{false};

    /// type=progress 的快捷方式。
    std::function<void(json)> progress;

    /// 发送任意类型的事件（batch/statement/progress...），最终事件仍由 Router 发送。
    std::function<void(const std::string&, json)> emit;

    /// 发送自定义 Bridge 事件（非 ipc.event），例如 session.closed。
    std::function<void(const std::string&, json)> emitNamed;

    bool IsCancelled() const { return cancelled.load(std::memory_order_relaxed); }

    /// 注册取消钩子（例如 sqlite3_interrupt）。可在任务运行中调用。
    void SetCancelHook(std::function<void()> hook) {
        std::lock_guard<std::mutex> lock(hook_mutex_);
        on_cancel_ = std::move(hook);
    }

    void ClearCancelHook() {
        std::lock_guard<std::mutex> lock(hook_mutex_);
        on_cancel_ = nullptr;
    }

    void InvokeCancelHook() {
        std::function<void()> hook;
        {
            std::lock_guard<std::mutex> lock(hook_mutex_);
            hook = on_cancel_;
        }
        if (!hook) return;
        try {
            hook();
        } catch (...) {
            // 取消钩子不允许把异常抛回 Router。
        }
    }

private:
    std::mutex hook_mutex_;
    std::function<void()> on_cancel_;
};

/// 异步任务路由：Bridge handler 在 UI 线程同步执行，这里只负责入队并立即返回，
/// 真正的数据库/文件任务在 worker 线程池执行，结果通过 `ipc.event` 事件回传。
///
/// 事件格式：
///   { "reqId": "...", "type": "progress", "data": {...} }
///   { "reqId": "...", "type": "result",   "data": {...} }
///   { "reqId": "...", "type": "error",    "error": { "code": "...", "message": "..." } }
///   { "reqId": "...", "type": "cancelled" }
class AsyncRouter {
public:
    using Handler = std::function<json(const json& args, TaskContext& ctx)>;

    AsyncRouter(tauricpp::Bridge& bridge, size_t worker_count);
    ~AsyncRouter();

    AsyncRouter(const AsyncRouter&) = delete;
    AsyncRouter& operator=(const AsyncRouter&) = delete;

    void Register(const std::string& command, Handler handler);
    bool Has(const std::string& command) const;

    /// 由 `ipc.call` 调用；返回 false 表示命令不存在或参数非法。
    bool Submit(const std::string& command, json args, const std::string& req_id);
    bool Cancel(const std::string& req_id);

    void Shutdown();

private:
    struct Task {
        std::string command;
        json args;
        std::string req_id;
        std::shared_ptr<TaskContext> ctx;
    };

    json HandleCall(const json& args);
    json HandleCancel(const json& args);

    void WorkerLoop();
    void Emit(const json& payload) const;
    void Finish(const std::string& req_id);

    tauricpp::Bridge& bridge_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Handler> handlers_;
    std::unordered_map<std::string, std::shared_ptr<TaskContext>> inflight_;

    std::queue<Task> queue_;
    std::condition_variable cv_;
    std::mutex queue_mutex_;
    std::vector<std::thread> workers_;
    std::atomic<bool> stopping_{false};
    std::atomic<uint64_t> submitted_{0};
};

}  // namespace silkit::ipc
