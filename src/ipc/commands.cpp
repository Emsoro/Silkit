#include "ipc/commands.h"

#include "app/version.h"
#include "core/logger.h"
#include "core/paths.h"

#include <tauricpp/window.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

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

void RegisterCommands(AsyncRouter& router, tauricpp::Window& window, Settings& settings) {
    router.Register("app.info", [](const json&, TaskContext&) -> json {
        return json{
            {"ok", true},
            {"name", kAppName},
            {"version", kVersion},
            {"build", std::string(__DATE__) + " " + __TIME__},
            {"platform", "windows"},
            {"engines", json::array({"sqlite", "mysql", "postgres"})},
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
}

}  // namespace silkit::ipc
