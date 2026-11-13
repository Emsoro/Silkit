#include "ipc/async_router.h"

#include "core/logger.h"

#include <utility>

namespace silkit::ipc {

AsyncRouter::AsyncRouter(tauricpp::Bridge& bridge, size_t worker_count) : bridge_(bridge) {
    bridge_.RegisterCommand("ipc.call", [this](const json& args) -> json { return HandleCall(args); });
    bridge_.RegisterCommand("ipc.cancel", [this](const json& args) -> json { return HandleCancel(args); });

    if (worker_count == 0) worker_count = 1;
    workers_.reserve(worker_count);
    for (size_t i = 0; i < worker_count; ++i) {
        workers_.emplace_back([this] { WorkerLoop(); });
    }
    LogInfo("ipc: AsyncRouter 启动，worker=" + std::to_string(worker_count));
}

AsyncRouter::~AsyncRouter() { Shutdown(); }

void AsyncRouter::Register(const std::string& command, Handler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[command] = std::move(handler);
}

bool AsyncRouter::Has(const std::string& command) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return handlers_.find(command) != handlers_.end();
}

json AsyncRouter::HandleCall(const json& args) {
    if (!args.is_object()) {
        return json{{"ok", false}, {"error", {{"code", "bad_args"}, {"message", "args 必须是对象"}}}};
    }

    const std::string command = args.value("cmd", std::string{});
    const std::string req_id = args.value("reqId", std::string{});
    json call_args = args.contains("args") ? args["args"] : json::object();

    if (command.empty() || req_id.empty()) {
        return json{{"ok", false},
                    {"error", {{"code", "bad_args"}, {"message", "缺少 cmd 或 reqId"}}}};
    }

    if (!Has(command)) {
        return json{{"ok", false},
                    {"error", {{"code", "unknown_command"}, {"message", "未知命令: " + command}}}};
    }

    if (!Submit(command, std::move(call_args), req_id)) {
        return json{{"ok", false},
                    {"error", {{"code", "rejected"}, {"message", "任务被拒绝: " + command}}}};
    }
    return json{{"ok", true}, {"reqId", req_id}};
}

json AsyncRouter::HandleCancel(const json& args) {
    const std::string req_id = args.value("reqId", std::string{});
    const bool found = Cancel(req_id);
    return json{{"ok", true}, {"found", found}};
}

bool AsyncRouter::Submit(const std::string& command, json args, const std::string& req_id) {
    Handler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = handlers_.find(command);
        if (it == handlers_.end()) return false;
        handler = it->second;
    }

    auto ctx = std::make_shared<TaskContext>();
    ctx->progress = [this, req_id](json data) {
        Emit(json{{"reqId", req_id}, {"type", "progress"}, {"data", std::move(data)}});
    };

    {
        std::lock_guard<std::mutex> lock(mutex_);
        inflight_[req_id] = ctx;
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (stopping_.load(std::memory_order_relaxed)) return false;
        queue_.push(Task{command, std::move(args), req_id, std::move(ctx)});
        ++submitted_;
    }
    cv_.notify_one();
    return true;
}

bool AsyncRouter::Cancel(const std::string& req_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = inflight_.find(req_id);
    if (it == inflight_.end()) return false;
    it->second->cancelled.store(true, std::memory_order_relaxed);
    LogInfo("ipc: 取消请求 " + req_id);
    return true;
}

void AsyncRouter::Finish(const std::string& req_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    inflight_.erase(req_id);
}

void AsyncRouter::Emit(const json& payload) const {
    bridge_.Emit("ipc.event", payload);
}

void AsyncRouter::WorkerLoop() {
    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_.wait(lock, [this] { return stopping_.load(std::memory_order_relaxed) || !queue_.empty(); });
            if (stopping_.load(std::memory_order_relaxed) && queue_.empty()) return;
            task = std::move(queue_.front());
            queue_.pop();
        }

        if (task.ctx->IsCancelled()) {
            Emit(json{{"reqId", task.req_id}, {"type", "cancelled"}});
            Finish(task.req_id);
            continue;
        }

        Handler handler;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = handlers_.find(task.command);
            if (it != handlers_.end()) handler = it->second;
        }

        if (!handler) {
            Emit(json{{"reqId", task.req_id},
                      {"type", "error"},
                      {"error", {{"code", "unknown_command"}, {"message", task.command}}}});
            Finish(task.req_id);
            continue;
        }

        try {
            json result = handler(task.args, *task.ctx);
            if (task.ctx->IsCancelled()) {
                Emit(json{{"reqId", task.req_id}, {"type", "cancelled"}});
            } else {
                Emit(json{{"reqId", task.req_id}, {"type", "result"}, {"data", std::move(result)}});
            }
        } catch (const std::exception& e) {
            LogError(std::string("ipc: ") + task.command + " 异常: " + e.what());
            Emit(json{{"reqId", task.req_id},
                      {"type", "error"},
                      {"error", {{"code", "exception"}, {"message", e.what()}}}});
        } catch (...) {
            LogError("ipc: " + task.command + " 未知异常");
            Emit(json{{"reqId", task.req_id},
                      {"type", "error"},
                      {"error", {{"code", "exception"}, {"message", "未知异常"}}}});
        }
        Finish(task.req_id);
    }
}

void AsyncRouter::Shutdown() {
    if (stopping_.exchange(true, std::memory_order_relaxed)) return;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        std::queue<Task> empty;
        queue_.swap(empty);
    }
    cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        inflight_.clear();
    }
    LogInfo("ipc: AsyncRouter 已停止");
}

}  // namespace silkit::ipc
