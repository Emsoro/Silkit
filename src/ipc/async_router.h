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
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace silkit::ipc {

using json = nlohmann::json;

/// 交给异步处理函数的上下文：进度回传 + 取消标记。
struct TaskContext {
    std::atomic<bool> cancelled{false};

    std::function<void(json)> progress;

    bool IsCancelled() const { return cancelled.load(std::memory_order_relaxed); }
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
