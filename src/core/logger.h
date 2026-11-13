#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace silkit {

enum class LogLevel { kDebug, kInfo, kWarn, kError };

/// 轻量线程安全日志：写入 %APPDATA%\Silkit\logs\silkit-YYYYMMDD.log。
/// 单文件超过 5MB 时轮转为 .1（保留一份），避免长期运行无限增长。
class Logger {
public:
    static Logger& Instance();

    bool Init(const std::filesystem::path& log_dir);
    void Shutdown();

    /// 当前日志文件路径（尚未 Init 时为空）。
    std::filesystem::path CurrentFile() const;

    void Write(LogLevel level, const std::string& message);

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void RotateIfNeeded();

    mutable std::mutex mutex_;
    std::filesystem::path log_dir_;
    std::filesystem::path file_path_;
    std::ofstream stream_;
    bool initialized_ = false;
};

void LogDebug(const std::string& message);
void LogInfo(const std::string& message);
void LogWarn(const std::string& message);
void LogError(const std::string& message);

}  // namespace silkit
