#include "core/profiles.h"

#include "core/json_merge.h"
#include "core/logger.h"
#include "core/paths.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <utility>

namespace silkit {
namespace {

nlohmann::json EmptyProfiles() {
    return nlohmann::json{{"version", 1}, {"connections", nlohmann::json::array()}};
}

bool ReadWholeFile(const std::filesystem::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::ostringstream ss;
    ss << file.rdbuf();
    out = ss.str();
    return true;
}

bool AtomicWrite(const std::filesystem::path& path, const std::string& content) {
    auto tmp = path;
    tmp += L".tmp";
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!file.good()) return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

std::string NewId() {
    static std::atomic<std::uint64_t> counter{0};
    const auto ticks = static_cast<std::uint64_t>(GetTickCount64());
    const std::uint64_t value = (ticks << 8) | (++counter & 0xFF);
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "c_%llx", static_cast<unsigned long long>(value));
    return buffer;
}

bool ValidEngine(const std::string& engine) {
    return engine == "sqlite" || engine == "mysql" || engine == "postgres";
}

}  // namespace

bool EnsureProfilesFile(const std::filesystem::path& file) {
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) return true;

    const std::string seed = R"({"version":1,"connections":[]})";
    if (!AtomicWrite(file, seed)) return false;
    LogInfo("profiles: 创建 " + Paths::ToUtf8(file));
    return true;
}

ProfileStore::ProfileStore(std::filesystem::path file) : file_(std::move(file)), data_(EmptyProfiles()) {}

bool ProfileStore::Load() {
    std::lock_guard<std::mutex> lock(mutex_);
    data_ = EmptyProfiles();

    std::string raw;
    if (!ReadWholeFile(file_, raw) || raw.empty()) {
        SaveLocked();
        return false;
    }

    try {
        const auto parsed = nlohmann::json::parse(raw);
        data_ = MergeJson(EmptyProfiles(), parsed);
        if (!data_["connections"].is_array()) data_["connections"] = nlohmann::json::array();
        return true;
    } catch (const std::exception& e) {
        LogWarn(std::string("profiles: 解析失败，使用空配置: ") + e.what());
        data_ = EmptyProfiles();
        SaveLocked();
        return false;
    }
}

nlohmann::json ProfileStore::List() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json list = nlohmann::json::array();
    for (const auto& profile : data_["connections"]) {
        list.push_back(Sanitize(profile));
    }
    return list;
}

nlohmann::json ProfileStore::Get(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& profile : data_["connections"]) {
        if (profile.value("id", std::string{}) == id) return profile;
    }
    return nullptr;
}

bool ProfileStore::Upsert(const nlohmann::json& profile, std::string& id, std::string& error) {
    if (!profile.is_object()) {
        error = "连接配置必须是对象";
        return false;
    }

    const std::string engine = profile.value("engine", std::string{"sqlite"});
    if (!ValidEngine(engine)) {
        error = "不支持的数据库引擎: " + engine;
        return false;
    }
    if (profile.value("name", std::string{}).empty()) {
        error = "连接名称不能为空";
        return false;
    }
    if (engine == "sqlite" && profile.value("file", std::string{}).empty()) {
        error = "SQLite 文件路径不能为空";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const std::string incoming = profile.value("id", std::string{});
    const std::string now = [] {
        const auto now_ticks = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now_ticks);
        std::tm utc{};
        gmtime_s(&utc, &time);
        char buffer[32] = {};
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
        return std::string(buffer);
    }();

    bool found = false;
    for (auto& existing : data_["connections"]) {
        if (existing.value("id", std::string{}) != incoming || incoming.empty()) continue;
        nlohmann::json merged = MergeJson(existing, profile);
        merged["id"] = incoming;
        if (!merged.contains("createdAt") || !merged["createdAt"].is_string() ||
            merged["createdAt"].get<std::string>().empty()) {
            merged["createdAt"] = now;
        }
        existing = std::move(merged);
        id = incoming;
        found = true;
        break;
    }

    if (!found) {
        nlohmann::json created = profile;
        id = incoming.empty() ? NewId() : incoming;
        created["id"] = id;
        if (!created.contains("createdAt") || !created["createdAt"].is_string() ||
            created["createdAt"].get<std::string>().empty()) {
            created["createdAt"] = now;
        }
        data_["connections"].push_back(std::move(created));
    }

    if (!SaveLocked()) {
        error = "写入连接配置失败";
        return false;
    }
    LogInfo("profiles: 保存连接 " + id);
    return true;
}

bool ProfileStore::Remove(const std::string& id, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& connections = data_["connections"];
    for (auto it = connections.begin(); it != connections.end(); ++it) {
        if (it->value("id", std::string{}) == id) {
            connections.erase(it);
            if (!SaveLocked()) {
                error = "写入连接配置失败";
                return false;
            }
            LogInfo("profiles: 删除连接 " + id);
            return true;
        }
    }
    error = "连接不存在: " + id;
    return false;
}

bool ProfileStore::Touch(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& profile : data_["connections"]) {
        if (profile.value("id", std::string{}) != id) continue;
        const auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm utc{};
        gmtime_s(&utc, &time);
        char buffer[32] = {};
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
        profile["lastUsedAt"] = buffer;
        return SaveLocked();
    }
    return false;
}

bool ProfileStore::SaveLocked() {
    if (file_.empty()) return false;
    const auto text = data_.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    if (!AtomicWrite(file_, text)) {
        LogWarn("profiles: 写入失败 " + Paths::ToUtf8(file_));
        return false;
    }
    return true;
}

nlohmann::json ProfileStore::Sanitize(const nlohmann::json& profile) {
    nlohmann::json copy = profile;
    // 只回传「是否已保存密码」，密文本身不出主进程。
    const auto secret = profile.value("secret", nlohmann::json::object());
    copy["hasSecret"] = secret.is_object() && secret.value("save", false);
    copy.erase("secret");
    copy.erase("password");
    return copy;
}

}  // namespace silkit
