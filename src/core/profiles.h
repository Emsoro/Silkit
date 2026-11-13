#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <mutex>
#include <string>

namespace silkit {

/// 保证 profiles.json 存在且结构合法。
bool EnsureProfilesFile(const std::filesystem::path& file);

/// connections.json/profiles.json 的读写。密码字段原样保存，List 永不回传。
class ProfileStore {
public:
    explicit ProfileStore(std::filesystem::path file);

    bool Load();

    /// 列表（去掉 secret/password）。
    nlohmann::json List() const;

    /// 内部完整配置（可能包含 secret）；找不到返回 null。
    nlohmann::json Get(const std::string& id) const;

    bool Upsert(const nlohmann::json& profile, std::string& id, std::string& error);
    bool Remove(const std::string& id, std::string& error);
    bool Touch(const std::string& id);

    const std::filesystem::path& File() const { return file_; }

private:
    bool SaveLocked();
    static nlohmann::json Sanitize(const nlohmann::json& profile);

    mutable std::mutex mutex_;
    std::filesystem::path file_;
    nlohmann::json data_;
};

}  // namespace silkit
