#include "core/paths.h"

#include <Windows.h>
#include <ShlObj.h>

#include <system_error>

namespace silkit {
namespace {

std::filesystem::path KnownRoamingAppData() {
    PWSTR raw = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &raw);
    if (FAILED(hr) || raw == nullptr) {
        if (raw) CoTaskMemFree(raw);
        return {};
    }
    std::filesystem::path result(raw);
    CoTaskMemFree(raw);
    return result;
}

}  // namespace

std::filesystem::path Paths::root_;

bool Paths::Initialize() {
    if (!root_.empty()) return true;

    const auto roaming = KnownRoamingAppData();
    if (roaming.empty()) return false;

    std::error_code ec;
    root_ = roaming / L"Silkit";
    std::filesystem::create_directories(root_, ec);
    if (ec) return false;

    std::filesystem::create_directories(root_ / L"logs", ec);
    return !ec;
}

const std::filesystem::path& Paths::Root() { return root_; }

std::filesystem::path Paths::LogsDir() { return root_ / L"logs"; }

std::filesystem::path Paths::SettingsFile() { return root_ / L"settings.json"; }

std::filesystem::path Paths::ProfilesFile() { return root_ / L"profiles.json"; }

std::filesystem::path Paths::UiStateFile() { return root_ / L"ui-state.json"; }

std::string Paths::ToUtf8(const std::filesystem::path& path) {
    if (path.empty()) return {};
    const auto& native = path.native();
    if (native.empty()) return {};

    const int size = WideCharToMultiByte(CP_UTF8, 0, native.c_str(), static_cast<int>(native.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, native.c_str(), static_cast<int>(native.size()), out.data(), size,
                        nullptr, nullptr);
    return out;
}

std::filesystem::path Paths::FromUtf8(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), size);
    return std::filesystem::path(wide);
}

}  // namespace silkit
