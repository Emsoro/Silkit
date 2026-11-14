#include "core/logger.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <system_error>
#include <vector>

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

bool IsKeyChar(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '-';
}

bool MatchKey(const std::string& lower, std::size_t pos, const char* key) {
    const std::size_t len = std::strlen(key);
    if (lower.compare(pos, len, key) != 0) return false;
    if (pos > 0 && IsKeyChar(lower[pos - 1])) return false;
    const std::size_t end = pos + len;
    if (end < lower.size() && IsKeyChar(lower[end])) return false;
    return true;
}

}  // namespace

std::string MaskSecrets(const std::string& message) {
    static const char* const kKeys[] = {
        "password", "passwd", "pwd", "secret", "token", "api_key", "apikey",
        "access_key", "private_key"};

    std::string lower = message;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    std::string out;
    out.reserve(message.size());
    std::size_t i = 0;
    while (i < message.size()) {
        bool matched = false;
        for (const char* key : kKeys) {
            const std::size_t len = std::strlen(key);
            if (!MatchKey(lower, i, key)) continue;

            std::size_t j = i + len;
            while (j < message.size() && (message[j] == ' ' || message[j] == '\t')) ++j;
            if (j >= message.size() || (message[j] != '=' && message[j] != ':')) break;
            out.append(message, i, j - i + 1);
            ++j;
            const std::size_t after_separator = j;
            while (j < message.size() && (message[j] == ' ' || message[j] == '\t')) ++j;

            const char quote = (j < message.size() && (message[j] == '"' || message[j] == '\''))
                                   ? message[j]
                                   : '\0';
            if (quote != '\0') ++j;
            const std::size_t value_start = j;
            // 保留分隔符后的空格与起始引号。
            out.append(message, after_separator, value_start - after_separator);
            while (j < message.size()) {
                const char ch = message[j];
                if (quote != '\0') {
                    if (ch == quote) break;
                } else if (std::isspace(static_cast<unsigned char>(ch)) || ch == ';' || ch == ',' ||
                           ch == '&' || ch == '}' || ch == ')' || ch == '"' || ch == '\'') {
                    break;
                }
                ++j;
            }
            if (j > value_start) out += "***";
            if (quote != '\0' && j < message.size() && message[j] == quote) {
                out.push_back(quote);
                ++j;
            }
            i = j;
            matched = true;
            break;
        }
        if (!matched) {
            out.push_back(message[i]);
            ++i;
        }
    }
    return out;
}

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
    stream_ << Timestamp() << " [" << LevelName(level) << "] " << MaskSecrets(message) << '\n';
    stream_.flush();
}

void LogDebug(const std::string& message) { Logger::Instance().Write(LogLevel::kDebug, message); }
void LogInfo(const std::string& message) { Logger::Instance().Write(LogLevel::kInfo, message); }
void LogWarn(const std::string& message) { Logger::Instance().Write(LogLevel::kWarn, message); }
void LogError(const std::string& message) { Logger::Instance().Write(LogLevel::kError, message); }

}  // namespace silkit
