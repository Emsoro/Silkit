// Silkit M0 冒烟测试：JSON 深合并 + 默认配置形状。
// 刻意不引入测试框架，保持单文件、零依赖，构建后由 CTest 直接运行。
#include "core/json_merge.h"
#include "core/logger.h"
#include "core/settings.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

int g_failed = 0;
int g_total = 0;

void Expect(bool condition, const std::string& name) {
    ++g_total;
    if (condition) return;
    ++g_failed;
    std::printf("  [FAIL] %s\n", name.c_str());
}

void ExpectString(const nlohmann::json& value, const std::string& expected, const std::string& name) {
    Expect(value.is_string() && value.get<std::string>() == expected, name);
}

}  // namespace

int main() {
    using nlohmann::json;
    using silkit::MergeJson;

    std::printf("Silkit smoke tests\n");

    // ---------------------------------------------------------------- MergeJson
    {
        const json base = {
            {"version", 1},
            {"ui", {{"theme", "bloom"}, {"appearance", "light"}, {"fontSize", 13}}},
            {"list", {1, 2, 3}},
        };
        const json patch = {
            {"ui", {{"theme", "noir"}, {"followSystem", false}}},
            {"list", {9}},
            {"added", true},
        };

        const json merged = MergeJson(base, patch);
        ExpectString(merged["ui"]["theme"], "noir", "patch 覆盖嵌套标量");
        Expect(merged["ui"]["appearance"] == "light", "未提供的键保持原值");
        Expect(merged["ui"]["fontSize"] == 13, "未提供的数字保持原值");
        Expect(merged["ui"]["followSystem"] == false, "嵌套新增键写入");
        Expect(merged["version"] == 1, "顶层未提供的键保持原值");
        Expect(merged["list"].is_array() && merged["list"].size() == 1 && merged["list"][0] == 9,
               "数组直接替换而非合并");
        Expect(merged["added"] == true, "顶层新增键写入");
    }

    {
        const json base = {{"a", 1}};
        const json array_patch = json::array({1, 2});
        Expect(MergeJson(base, array_patch) == array_patch, "非对象 patch 直接替换");
        Expect(MergeJson(json(5), base) == base, "非对象 base 直接替换");
    }

    {
        const json base = {{"ui", {{"theme", "bloom"}}}};
        const json empty = json::object();
        Expect(MergeJson(base, empty) == base, "空对象 patch 不改动 base");
    }

    // ---------------------------------------------------------------- 默认配置
    {
        const json defaults = silkit::Settings::Defaults();
        Expect(defaults.is_object(), "Defaults 是对象");
        Expect(defaults["version"] == 1, "配置版本为 1");
        ExpectString(defaults["ui"]["theme"], "bloom", "默认主题 bloom");
        ExpectString(defaults["ui"]["appearance"], "light", "默认外观 light");
        Expect(defaults["ui"]["followSystem"] == false, "默认不跟随系统");
        Expect(defaults["ui"]["editorRatio"].is_number(), "editorRatio 是数字");
        Expect(defaults["query"]["maxRows"] == 5000, "默认最大行数 5000");
        Expect(defaults["query"]["safeMode"] == true, "默认开启安全模式");
        Expect(defaults["data"]["pageSize"] == 500, "默认分页 500");
        ExpectString(defaults["export"]["defaultEncoding"], "utf-8", "默认导出编码");
        Expect(defaults["editor"]["tabSize"] == 2, "默认 tabSize 2");
    }

    // ---------------------------------------------------------------- 日志文件名
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "silkit_logger_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);

        silkit::Logger::Instance().Init(dir);
        std::printf("  logger path: %s\n",
                    silkit::Logger::Instance().CurrentFile().u8string().c_str());
        silkit::LogInfo("logger smoke test");
        silkit::Logger::Instance().Shutdown();

        bool found_log = false;
        for (const auto& entry : fs::directory_iterator(dir)) {
            const std::string name = entry.path().filename().string();
            std::printf("  log file: %s\n", name.c_str());
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".log") == 0) found_log = true;
        }
        Expect(found_log, "日志文件使用 .log 扩展名");
        fs::remove_all(dir, ec);
    }

    if (g_failed == 0) {
        std::printf("  %d checks passed\n", g_total);
        return 0;
    }
    std::printf("  %d/%d checks failed\n", g_failed, g_total);
    return 1;
}
