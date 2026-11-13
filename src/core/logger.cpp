#include "core/logger.h"

#include <Windows.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <system_error>

namespace silkit {
namespace {

std::string Timestamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u", st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::string Today() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "%04u%02u%02u", st.wYear, st.wMonth, st.wDay);
    return buf;
}

const char* LevelName(LogLevel level) {
    switch (level) {
        case LogLevel::kDebug: return "DEBUG";
        case LogLevel::kInfo: return "INFO";
        case LogLevel::kWarn: return "WARN";
        case LogLevel::kError: return "ERROR";
    }
    return "INFO";
}

}  // namespace

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

bool Logger::Init(const std::filesystem::path& log_dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    log_dir_ = log_dir;
    std::error_code ec;
    std::filesystem::create_directories(log_dir_, ec);
    if (ec) return false;

    const std::string today = Today();
    const std::wstring day(today.begin(), today.end());
    std::wstring filename = L"silkit-";
    filename.append(day);
    filename.append(L".log");
    file_path_ = log_dir_ / filename;
    stream_.open(file_path_, std::ios::binary | std::ios::app);
    initialized_ = stream_.is_open();
    return initialized_;
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_.is_open()) stream_.close();
    initialized_ = false;
}

std::filesystem::path Logger::CurrentFile() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return file_path_;
}

void Logger::RotateIfNeeded() {
    if (!stream_.is_open()) return;
    stream_.flush();
    std::error_code ec;
    const auto size = std::filesystem::file_size(file_path_, ec);
    if (ec || size < 5ull * 1024 * 1024) return;

    stream_.close();
    auto backup = file_path_;
    backup += L".1";
    std::filesystem::remove(backup, ec);
    std::filesystem::rename(file_path_, backup, ec);
    stream_.open(file_path_, std::ios::binary | std::ios::app);
}

void Logger::Write(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || !stream_.is_open()) return;

    RotateIfNeeded();
    stream_ << Timestamp() << " [" << LevelName(level) << "] " << message << '\n';
    stream_.flush();
}

void LogDebug(const std::string& message) { Logger::Instance().Write(LogLevel::kDebug, message); }
void LogInfo(const std::string& message) { Logger::Instance().Write(LogLevel::kInfo, message); }
void LogWarn(const std::string& message) { Logger::Instance().Write(LogLevel::kWarn, message); }
void LogError(const std::string& message) { Logger::Instance().Write(LogLevel::kError, message); }

}  // namespace silkit
