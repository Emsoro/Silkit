#include "ipc/commands.h"

#include "app/version.h"
#include "core/logger.h"
#include "core/paths.h"
#include "ipc/commands_conn.h"
#include "ipc/commands_ddl.h"
#include "ipc/commands_io.h"
#include "ipc/commands_meta.h"
#include "ipc/commands_query.h"
#include "ipc/commands_table.h"

#include <tauricpp/dialog.hpp>
#include <tauricpp/window.hpp>

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace silkit::ipc {
namespace {

struct ThemeInfo {
    const char* id;
    const char* name;
    const char* appearance;
    const char* bg;
    const char* accent;
};

constexpr std::array<ThemeInfo, 5> kThemes{{
    {"bloom", "晨樱 Bloom", "light", "#FFF7F9", "#C2255C"},
    {"noir", "夜樱 Noir", "dark", "#17121A", "#F06595"},
    {"lilac", "薰衣草 Lilac", "light", "#FAF7FF", "#6D4FC4"},
    {"mocha", "奶咖 Mocha", "light", "#FBF7F3", "#96563D"},
    {"mint", "薄荷 Mint", "light", "#F5FBF9", "#237668"},
}};

const ThemeInfo* FindTheme(const std::string& id) {
    for (const auto& theme : kThemes) {
        if (id == theme.id) return &theme;
    }
    return nullptr;
}

COLORREF HexToColorRef(const std::string& hex) {
    if (hex.size() != 7 || hex[0] != '#') return RGB(255, 247, 249);
    auto channel = [&hex](size_t offset) -> BYTE {
        return static_cast<BYTE>(std::stoi(hex.substr(offset, 2), nullptr, 16));
    };
    return RGB(channel(1), channel(3), channel(5));
}

/// WebView2 覆盖了客户区，窗口底色只在缩放/还原的瞬间露出；
/// 用 SetClassLongPtr 换成主题底色，避免深色主题下闪白。
void ApplyWindowBackground(HWND hwnd, const std::string& hex) {
    if (!hwnd) return;

    static std::mutex brush_mutex;
    static std::unordered_map<std::string, HBRUSH> brushes;

    HBRUSH brush = nullptr;
    {
        std::lock_guard<std::mutex> lock(brush_mutex);
        auto it = brushes.find(hex);
        if (it == brushes.end()) {
            it = brushes.emplace(hex, CreateSolidBrush(HexToColorRef(hex))).first;
        }
        brush = it->second;
    }

    SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(brush));
    InvalidateRect(hwnd, nullptr, TRUE);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
}

}  // namespace

std::string ThemeBackground(const std::string& theme_id) {
    const ThemeInfo* theme = FindTheme(theme_id);
    return theme ? theme->bg : "#FFF7F9";
}

void SetWindowBackground(tauricpp::Window& window, const std::string& theme_id) {
    ApplyWindowBackground(window.GetHwnd(), ThemeBackground(theme_id));
}

void RegisterCommands(AsyncRouter& router,
                      tauricpp::Window& window,
                      Settings& settings,
                      ProfileStore& profiles,
                      session::SessionManager& sessions) {
    router.Register("app.info", [](const json&, TaskContext&) -> json {
        return json{
            {"ok", true},
            {"name", kAppName},
            {"version", kVersion},
            {"build", std::string(__DATE__) + " " + __TIME__},
            {"platform", "windows"},
            {"engines", json::array({"sqlite", "mysql", "postgres"})},
            {"plannedEngines", json::array()},
            {"dataDir", Paths::ToUtf8(Paths::Root())},
        };
    });

    router.Register("app.ping", [](const json& args, TaskContext& ctx) -> json {
        const int steps = std::clamp(args.value("steps", 3), 1, 10);
        const int delay_ms = std::clamp(args.value("delayMs", 180), 0, 2000);
        const auto started = std::chrono::steady_clock::now();

        for (int i = 1; i <= steps; ++i) {
            if (ctx.IsCancelled()) break;
            ctx.progress(json{{"step", i}, {"total", steps}, {"message", "异步通道正常 " + std::to_string(i)}});
            for (int slept = 0; slept < delay_ms && !ctx.IsCancelled(); slept += 30) {
                std::this_thread::sleep_for(std::chrono::milliseconds(std::min(30, delay_ms - slept)));
            }
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
        return json{{"pong", true}, {"steps", steps}, {"elapsedMs", elapsed}};
    });

    router.Register("app.log", [](const json& args, TaskContext&) -> json {
        const std::string level = args.value("level", "info");
        const std::string message = args.value("message", "");
        if (level == "error") {
            LogError("[frontend] " + message);
        } else if (level == "warn") {
            LogWarn("[frontend] " + message);
        } else {
            LogInfo("[frontend] " + message);
        }
        return json{{"ok", true}};
    });

    router.Register("app.quit", [&window](const json&, TaskContext&) -> json {
        window.Close();
        return json{{"ok", true}};
    });

    router.Register("settings.get", [&settings](const json&, TaskContext&) -> json {
        return json{{"settings", settings.Snapshot()}};
    });

    router.Register("settings.set", [&settings](const json& args, TaskContext&) -> json {
        const json patch = args.value("patch", json::object());
        return json{{"settings", settings.Patch(patch)}};
    });

    router.Register("settings.reset", [&settings](const json&, TaskContext&) -> json {
        return json{{"settings", settings.Reset()}};
    });

    router.Register("app.paths", [](const json&, TaskContext&) -> json {
        return json{{"dataDir", Paths::ToUtf8(Paths::Root())},
                    {"logsDir", Paths::ToUtf8(Paths::LogsDir())},
                    {"settingsFile", Paths::ToUtf8(Paths::SettingsFile())},
                    {"logFile", Paths::ToUtf8(Logger::Instance().CurrentFile())}};
    });

    // 仅允许打开数据目录 / 日志目录，避免变成任意路径启动器。
    router.Register("app.open_path", [](const json& args, TaskContext&) -> json {
        const std::string kind = args.value("kind", std::string{"data"});
        if (kind != "data" && kind != "logs") {
            return json{{"ok", false},
                        {"error", {{"code", "bad_args"}, {"message", "仅支持 data / logs"}}}};
        }
        const std::filesystem::path path =
            kind == "logs" ? Paths::LogsDir() : Paths::Root();
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        const HINSTANCE result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr,
                                               SW_SHOWNORMAL);
        return json{{"ok", reinterpret_cast<INT_PTR>(result) > 32},
                    {"path", Paths::ToUtf8(path)}};
    });

    router.Register("theme.list", [](const json&, TaskContext&) -> json {
        json list = json::array();
        for (const auto& theme : kThemes) {
            list.push_back(json{{"id", theme.id},
                                {"name", theme.name},
                                {"appearance", theme.appearance},
                                {"bg", theme.bg},
                                {"accent", theme.accent}});
        }
        return json{{"themes", list}};
    });

    router.Register("theme.set", [&window, &settings](const json& args, TaskContext&) -> json {
        const std::string id = args.value("theme", std::string{"bloom"});
        const ThemeInfo* theme = FindTheme(id);
        if (!theme) {
            return json{{"ok", false},
                        {"error", {{"code", "bad_theme"}, {"message", "未知主题: " + id}}}};
        }

        if (args.contains("persist") && args["persist"].is_boolean() && !args["persist"].get<bool>()) {
            ApplyWindowBackground(window.GetHwnd(), theme->bg);
            return json{{"ok", true}, {"theme", theme->id}, {"windowBg", theme->bg}};
        }

        settings.Patch(json{{"ui", {{"theme", theme->id}, {"appearance", theme->appearance}, {"followSystem", false}}}});
        ApplyWindowBackground(window.GetHwnd(), theme->bg);
        LogInfo(std::string("theme: 切换到 ") + theme->id);
        return json{{"ok", true}, {"theme", theme->id}, {"windowBg", theme->bg}};
    });

    router.Register("app.pick_file", [&window](const json& args, TaskContext&) -> json {
        const std::string kind = args.value("kind", std::string{"sqlite"});
        const bool save = args.value("save", false);
        const std::string default_name = args.value("defaultName", std::string{});
        const std::string default_path = args.value("defaultPath", std::string{});

        std::vector<tauricpp::Dialog::FileFilter> filters;
        if (kind == "sqlite") {
            filters = {{"SQLite 数据库", "*.db;*.sqlite;*.sqlite3;*.db3"}, {"所有文件", "*.*"}};
        } else if (kind == "sql") {
            filters = {{"SQL 文件", "*.sql"}, {"所有文件", "*.*"}};
        } else if (kind == "csv") {
            filters = {{"CSV 文件", "*.csv"}, {"所有文件", "*.*"}};
        } else {
            filters = {{"所有文件", "*.*"}};
        }

        if (save) {
            tauricpp::Dialog::SaveOptions options;
            options.title = kind == "sqlite" ? "新建 SQLite 数据库" : "保存文件";
            options.default_path = default_path;
            options.default_filename = default_name;
            options.filters = std::move(filters);
            const auto path = tauricpp::Dialog::SaveFile(window.GetHwnd(), options);
            return json{{"ok", true}, {"path", path.value_or(std::string{})}};
        }

        tauricpp::Dialog::OpenOptions options;
        options.title = kind == "sqlite" ? "打开 SQLite 数据库" : "打开文件";
        options.default_path = default_path;
        options.filters = std::move(filters);
        const auto paths = tauricpp::Dialog::OpenFile(window.GetHwnd(), options);
        return json{{"ok", true},
                    {"path", paths.empty() ? std::string{} : paths.front()}};
    });

    // M4-8：.sql 文本文件读写。仅允许明确的文本扩展名，避免 IPC 被当作任意文件读取通道。
    const auto is_text_path = [](const std::string& path) {
        static const std::array<const char*, 6> exts{".sql", ".txt", ".csv", ".tsv", ".json", ".md"};
        if (path.size() < 4 || path.size() > 1024) return false;
        for (const char* ext : exts) {
            const std::size_t len = std::strlen(ext);
            if (path.size() >= len) {
                std::string tail = path.substr(path.size() - len);
                std::transform(tail.begin(), tail.end(), tail.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (tail == ext) return true;
            }
        }
        return false;
    };

    router.Register("app.read_file", [is_text_path](const json& args, TaskContext&) -> json {
        const std::string path = args.value("path", std::string{});
        if (!is_text_path(path)) {
            return json{{"ok", false},
                        {"error", {{"code", "bad_args"},
                                   {"message", "仅支持读取 .sql/.txt/.csv/.tsv/.json/.md 文本文件"}}}};
        }
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec || size > 16 * 1024 * 1024) {
            return json{{"ok", false},
                        {"error", {{"code", "io_error"},
                                   {"message", ec ? "文件不存在或无法访问" : "文件超过 16 MB 上限"}}}};
        }
        std::ifstream file(std::filesystem::path(path), std::ios::binary);
        if (!file) {
            return json{{"ok", false},
                        {"error", {{"code", "io_error"}, {"message", "无法打开文件"}}}};
        }
        std::string content(static_cast<std::size_t>(size), '\0');
        file.read(content.data(), size);
        content.resize(static_cast<std::size_t>(file.gcount()));
        // 去掉 UTF-8 BOM，编辑器里按 UTF-8 处理。
        if (content.size() >= 3 && content.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            content.erase(0, 3);
        }
        return json{{"ok", true},
                    {"path", path},
                    {"content", content},
                    {"bytes", content.size()}};
    });

    router.Register("app.save_file", [is_text_path](const json& args, TaskContext&) -> json {
        const std::string path = args.value("path", std::string{});
        const std::string content = args.value("content", std::string{});
        if (!is_text_path(path)) {
            return json{{"ok", false},
                        {"error", {{"code", "bad_args"},
                                   {"message", "仅支持保存 .sql/.txt/.csv/.tsv/.json/.md 文本文件"}}}};
        }
        std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
        if (!file) {
            return json{{"ok", false},
                        {"error", {{"code", "io_error"}, {"message", "无法写入文件（可能只读或被占用）"}}}};
        }
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        file.close();
        if (!file) {
            return json{{"ok", false},
                        {"error", {{"code", "io_error"}, {"message", "写入未完成"}}}};
        }
        return json{{"ok", true}, {"path", path}, {"bytes", content.size()}};
    });

    RegisterConnectionCommands(router, profiles, sessions);
    RegisterMetadataCommands(router, sessions);
    RegisterQueryCommands(router, sessions, settings);
    RegisterTableCommands(router, sessions, settings);
    RegisterDdlCommands(router, sessions);
    RegisterIoCommands(router, sessions, settings);
}

}  // namespace silkit::ipc
