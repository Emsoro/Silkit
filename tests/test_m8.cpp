// M8 基础能力测试：日志脱敏、设置备份/恢复/重置、危险语句识别与确认令牌。
#include "core/logger.h"
#include "core/settings.h"
#include "db/sql_splitter.h"
#include "db/sql_util.h"
#include "db/types.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace silkit;

int g_failed = 0;
int g_total = 0;

void Expect(bool condition, const std::string& name) {
    ++g_total;
    if (condition) return;
    ++g_failed;
    std::printf("  [FAIL] %s\n", name.c_str());
}

void TestMaskSecrets() {
    Expect(MaskSecrets("password=secret123 user=alice") == "password=*** user=alice",
           "mask: password=value");
    Expect(MaskSecrets("Password: \"p@ss word\"") == "Password: \"***\"",
           "mask: quoted value keeps quotes");
    Expect(MaskSecrets("dsn=host=localhost;user=u;pwd=topsecret;db=x") ==
               "dsn=host=localhost;user=u;pwd=***;db=x",
           "mask: semicolon terminated value");
    Expect(MaskSecrets("token=abc&page=1") == "token=***&page=1", "mask: ampersand boundary");
    Expect(MaskSecrets("SELECT * FROM users WHERE name = 'pwd'") ==
               "SELECT * FROM users WHERE name = 'pwd'",
           "mask: plain text untouched");
    Expect(MaskSecrets("pwd") == "pwd", "mask: bare key untouched");
    Expect(MaskSecrets("api_key='a b c'") == "api_key='***'", "mask: underscore key + quote");
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void WriteFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void TestSettingsBackupAndReset() {
    const auto root = std::filesystem::temp_directory_path() / "silkit_m8_settings";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "logs", ec);
    Logger::Instance().Init(root / "logs");

    const auto file = root / "settings.json";
    const auto backup = root / "settings.json.bak";
    Settings settings;

    settings.Load(file);
    Expect(std::filesystem::exists(file), "settings: 首次加载写入默认配置");

    settings.Patch(nlohmann::json{{"ui", {{"language", "en-US"}}},
                                  {"query", {{"safeMode", false}}}});
    // 再保存一次，让 .bak 保存“英文 + 关闭安全模式”的上一版配置。
    settings.Patch(nlohmann::json{{"ui", {{"theme", "noir"}}}});
    Expect(std::filesystem::exists(backup), "settings: 第二次保存生成 .bak");

    const auto saved = nlohmann::json::parse(ReadFile(file));
    Expect(saved["ui"]["language"] == "en-US", "settings: patch 生效");
    Expect(saved["query"]["safeMode"] == false, "settings: 嵌套 patch 合并");

    // 主文件损坏后，从 .bak 恢复（备份内容是上一次成功保存的配置）。
    WriteFile(file, "{ this is not json");
    Settings recovered;
    recovered.Load(file);
    const auto snapshot = recovered.Snapshot();
    Expect(snapshot["ui"]["language"] == "en-US", "settings: 从 .bak 恢复语言");
    Expect(nlohmann::json::parse(ReadFile(file))["ui"]["language"] == "en-US",
           "settings: 恢复后重写主配置");

    const auto reset = recovered.Reset();
    Expect(reset["ui"]["language"] == "zh-CN", "settings: reset 返回默认语言");
    Expect(reset["query"]["safeMode"] == true, "settings: reset 恢复安全模式");
    Expect(nlohmann::json::parse(ReadFile(file))["ui"]["language"] == "zh-CN",
           "settings: reset 写盘");

    Logger::Instance().Shutdown();
    std::filesystem::remove_all(root, ec);
}

void TestDestructiveStatements() {
    const std::string sql =
        "SELECT 1;\n"
        "DROP TABLE users;\n"
        "UPDATE users SET active = 0;\n"
        "DELETE FROM users WHERE id = 1;\n"
        "ALTER TABLE users ADD COLUMN note TEXT;\n";
    const auto statements = db::SplitSql(sql, db::SplitOptionsForEngine(
                                                    static_cast<int>(db::Engine::Sqlite)));
    Expect(statements.size() == 5, "safety: 语句分割数量");

    const auto hits = db::FindDestructiveStatements(statements);
    Expect(hits.size() == 3, "safety: 命中 DROP/UPDATE/ALTER");
    if (hits.size() == 3) {
        Expect(hits[0].index == 1, "safety: DROP 序号");
        Expect(hits[1].index == 2, "safety: 无 WHERE 的 UPDATE");
        Expect(hits[2].index == 4, "safety: ALTER 序号");
    }

    const auto token = db::DestructiveToken(statements);
    Expect(token == db::DestructiveToken(statements), "safety: 令牌稳定");
    auto changed = statements;
    changed[1].text = "DROP TABLE orders;";
    Expect(token != db::DestructiveToken(changed), "safety: SQL 变化使令牌失效");

    Expect(!db::IsDestructiveStatement("DELETE FROM users WHERE id = 1"),
           "safety: 带 WHERE 的 DELETE 不拦截");
    Expect(db::IsDestructiveStatement("TRUNCATE TABLE users"), "safety: TRUNCATE 拦截");
}

}  // namespace

int main() {
    std::printf("Silkit M8 tests\n");
    TestMaskSecrets();
    TestSettingsBackupAndReset();
    TestDestructiveStatements();

    std::printf("%d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
