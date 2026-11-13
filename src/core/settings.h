#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <mutex>

namespace silkit {

/// settings.json 的读写与内存快照。
/// - Load 时用默认值兜底，旧版本缺少的新键会自动补齐；
/// - Patch 递归合并后立即原子写盘；
/// - 所有访问线程安全（IPC worker 与 UI 线程都可能读写）。
class Settings {
public:
    bool Load(const std::filesystem::path& file);
    nlohmann::json Snapshot() const;
    nlohmann::json Patch(const nlohmann::json& patch);
    void Flush();

    /// 默认配置（与 CODEPLAN §5.3 保持一致）
    static nlohmann::json Defaults();

private:
    void SaveLocked();

    mutable std::mutex mutex_;
    std::filesystem::path file_;
    nlohmann::json data_;
    bool dirty_ = false;
};

}  // namespace silkit
