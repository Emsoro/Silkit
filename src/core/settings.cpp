#include "core/settings.h"

#include "core/json_merge.h"
#include "core/logger.h"
#include "core/paths.h"

#include <Windows.h>

#include <fstream>
#include <sstream>

namespace silkit {
namespace {

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

/// 保留上一份可解析配置；写坏之后还能从 .bak 恢复。
void BackupExisting(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return;
    auto backup = path;
    backup += L".bak";
    CopyFileW(path.c_str(), backup.c_str(), FALSE);
}

bool ParseInto(const std::string& raw, nlohmann::json& out) {
    if (raw.empty()) return false;
    try {
        auto parsed = nlohmann::json::parse(raw);
        if (!parsed.is_object()) return false;
        out = std::move(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace

nlohmann::json Settings::Defaults() {
    return nlohmann::json::parse(R"json({
  "version": 1,
  "ui": {
    "theme": "bloom",
    "appearance": "light",
    "followSystem": false,
    "reduceMotion": false,
    "language": "zh-CN",
    "fontSize": 13,
    "gridRowHeight": 26,
    "sidebarWidth": 264,
    "editorRatio": 0.44,
    "restoreTabs": true,
    "confirmDestructive": true
  },
  "editor": {
    "fontSize": 13,
    "tabSize": 2,
    "wordWrap": false,
    "autocomplete": true,
    "showLineNumbers": true
  },
  "query": {
    "maxRows": 5000,
    "fetchSize": 500,
    "timeoutMs": 0,
    "autoLimit": true,
    "historyLimit": 5000,
    "safeMode": true
  },
  "data": {
    "pageSize": 500,
    "nullDisplay": "NULL",
    "dateFormat": "YYYY-MM-DD HH:mm:ss",
    "binaryPreviewBytes": 65536,
    "maxCellBytes": 262144
  },
  "export": {
    "defaultEncoding": "utf-8",
    "csvDelimiter": ",",
    "includeHeader": true,
    "nullLiteral": ""
  }
})json");
}

bool Settings::Load(const std::filesystem::path& file) {
    std::lock_guard<std::mutex> lock(mutex_);
    file_ = file;
    data_ = Defaults();

    std::string raw;
    if (!ReadWholeFile(file_, raw) || raw.empty()) {
        dirty_ = true;
        SaveLocked();
        LogInfo("settings: 创建默认配置 " + Paths::ToUtf8(file_));
        return true;
    }

    try {
        auto parsed = nlohmann::json::parse(raw);
        data_ = MergeJson(Defaults(), parsed);
    } catch (const std::exception& e) {
        LogWarn(std::string("settings: 解析失败，尝试从备份恢复: ") + e.what());
        auto backup = file_;
        backup += L".bak";
        std::string backup_raw;
        nlohmann::json parsed;
        if (ReadWholeFile(backup, backup_raw) && ParseInto(backup_raw, parsed)) {
            data_ = MergeJson(Defaults(), parsed);
            dirty_ = true;
            // 不再覆盖 .bak（主文件已经损坏）。
            SaveLocked(false);
            LogWarn("settings: 已从 " + Paths::ToUtf8(backup) + " 恢复");
            return true;
        }
        data_ = Defaults();
        dirty_ = true;
        SaveLocked();
    }
    return true;
}

nlohmann::json Settings::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return data_;
}

nlohmann::json Settings::Patch(const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!patch.is_object()) return data_;
    data_ = MergeJson(data_, patch);
    dirty_ = true;
    SaveLocked();
    return data_;
}

nlohmann::json Settings::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    data_ = Defaults();
    dirty_ = true;
    SaveLocked();
    return data_;
}

void Settings::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (dirty_) SaveLocked();
}

void Settings::SaveLocked(bool backup) {
    if (file_.empty()) return;
    if (backup) {
        // 超大配置（例如内含大量历史）不做备份，避免每次 patch 都复制大文件。
        std::error_code ec;
        const auto size = std::filesystem::file_size(file_, ec);
        if (!ec && size > 0 && size <= 1024 * 1024) BackupExisting(file_);
    }
    const auto text = data_.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    if (!AtomicWrite(file_, text)) {
        LogWarn("settings: 写入失败 " + Paths::ToUtf8(file_));
        return;
    }
    dirty_ = false;
}

}  // namespace silkit
