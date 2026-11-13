#pragma once

#include <filesystem>
#include <string>

namespace silkit {

/// 应用数据目录（%APPDATA%\Silkit）及其子路径。
/// Initialize 只调用一次，之后所有路径访问都是只读的。
class Paths {
public:
    static bool Initialize();

    static const std::filesystem::path& Root();
    static std::filesystem::path LogsDir();
    static std::filesystem::path SettingsFile();
    static std::filesystem::path ProfilesFile();
    static std::filesystem::path UiStateFile();

    static std::string ToUtf8(const std::filesystem::path& path);
    static std::filesystem::path FromUtf8(const std::string& utf8);

private:
    static std::filesystem::path root_;
};

}  // namespace silkit
