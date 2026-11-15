// Silkit M0 冒烟测试：JSON 深合并 + 默认配置形状。
// 刻意不引入测试框架，保持单文件、零依赖，构建后由 CTest 直接运行。
#include "core/json_merge.h"
#include "core/logger.h"
#include "core/settings.h"
#include "db/drivers/sqlite_driver.h"
#include "db/identifier.h"
#include "db/session_manager.h"
#include "db/sql_splitter.h"
#include "db/sql_util.h"
#include "db/type_map.h"
#include "util/base64.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failed = 0;
int g_total = 0;

// C++20 起 path::u8string() 返回 std::u8string(char8_t)，而 ConnectParams/printf
// 需要 UTF-8 的 std::string；逐字节 reinterpret 保持路径编码不变。
std::string Utf8Path(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

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

    // ---------------------------------------------------------------- Base64
    {
        const std::string bytes("\x00\x01\xff", 3);
        const std::string encoded = silkit::util::Base64Encode(bytes.data(), bytes.size());
        Expect(encoded == "AAH/", "base64 编码");
        std::string decoded;
        Expect(silkit::util::Base64Decode("AAH/", decoded) && decoded == bytes, "base64 解码");
        Expect(!silkit::util::Base64Decode("!!!!", decoded), "base64 非法输入");
    }

    // ---------------------------------------------------------------- 标识符
    {
        using silkit::db::Engine;
        using silkit::db::QuoteIdentifier;
        using silkit::db::QuoteStringLiteral;
        Expect(QuoteIdentifier(Engine::Sqlite, "a\"b") == "\"a\"\"b\"", "SQLite 标识符转义");
        Expect(QuoteIdentifier(Engine::MySql, "a`b") == "`a``b`", "MySQL 标识符转义");
        Expect(QuoteStringLiteral("a'b") == "'a''b'", "字符串字面量转义");
        Expect(!silkit::db::IsSafeIdentifier(std::string("bad\nname")), "拒绝控制字符");
    }

    // ---------------------------------------------------------------- 类型映射
    {
        using silkit::db::KindFromDeclType;
        using silkit::db::ValueKind;
        Expect(KindFromDeclType("BIGINT") == ValueKind::Int, "bigint → int");
        Expect(KindFromDeclType("numeric(10,2)") == ValueKind::Decimal, "numeric → decimal");
        Expect(KindFromDeclType("timestamp with time zone") == ValueKind::Timestamp,
               "timestamptz → timestamp");
        Expect(KindFromDeclType("jsonb") == ValueKind::Json, "jsonb → json");
        Expect(KindFromDeclType("bytea") == ValueKind::Binary, "bytea → binary");
    }

    // ---------------------------------------------------------------- SQL 分割器
    {
        using silkit::db::SplitOptions;
        using silkit::db::SplitSql;

        const std::string script =
            "SELECT 1;\n"
            "-- 注释里的分号 ; 不切分\n"
            "SELECT ';' AS semi;\n"
            "SELECT \"a;b\" AS quoted;\n";
        const auto statements = SplitSql(script, SplitOptions{});
        Expect(statements.size() == 3, "分割器：3 条语句");
        if (statements.size() == 3) {
            Expect(statements[0].text == "SELECT 1;", "分割器：第一条");
            Expect(statements[1].text.find("SELECT ';'") != std::string::npos, "分割器：引号内分号");
            Expect(statements[2].text.find("SELECT \"a;b\"") == 0, "分割器：双引号内分号");
            Expect(statements[1].begin_offset > statements[0].begin_offset, "分割器：偏移递增");
        }

        SplitOptions pg;
        pg.dollar_quotes = true;
        const auto pg_statements = SplitSql("SELECT $$a;b$$; SELECT 2;", pg);
        Expect(pg_statements.size() == 2, "分割器：dollar quote");

        SplitOptions mysql;
        mysql.delimiter_command = true;
        mysql.backslash_escapes = true;
        const auto mysql_statements = SplitSql(
            "DELIMITER //\n"
            "CREATE PROCEDURE p() BEGIN SELECT 1; END//\n"
            "DELIMITER ;\n"
            "SELECT 2;\n",
            mysql);
        Expect(mysql_statements.size() == 2, "分割器：DELIMITER");
    }

    // ---------------------------------------------------------------- SQL 分类
    {
        Expect(silkit::db::IsReadOnlyStatement("  -- x\n SELECT 1"), "只读：SELECT");
        Expect(silkit::db::IsReadOnlyStatement("PRAGMA table_info('t')"), "只读：PRAGMA");
        Expect(!silkit::db::IsReadOnlyStatement("UPDATE t SET a = 1"), "写：UPDATE");
        Expect(silkit::db::IsDestructiveStatement("DELETE FROM t"), "危险：无 WHERE DELETE");
    }

    // ---------------------------------------------------------------- SQLite 驱动
    {
        using namespace silkit::db;
        SqliteDriver driver;
        ConnectParams params;
        params.engine = Engine::Sqlite;
        params.file = ":memory:";
        DbError error;
        auto connection = driver.Open(params, error);
        Expect(connection != nullptr, "SQLite 打开内存库");
        if (connection) {
            ExecOptions options;
            auto exec = [&connection, &options](const std::string& sql,
                                                const std::vector<Param>& params_value = {}) {
                return connection->Execute(sql, params_value, options, nullptr);
            };

            auto created = exec(
                "CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT, score DECIMAL(10,2), "
                "data BLOB, note TEXT)");
            Expect(created.error.code == 0, "SQLite 建表");

            const std::vector<Param> insert_params{
                Param{ValueKind::Int, false, "1"},
                Param{ValueKind::Text, false, "alice"},
                Param{ValueKind::Decimal, false, "12.50"},
                Param{ValueKind::Binary, false, "AQID"},
                Param{ValueKind::Null, true, ""}};
            auto inserted = exec(
                "INSERT INTO users(id,name,score,data,note) VALUES(?,?,?,?,?)", insert_params);
            Expect(inserted.error.code == 0 && inserted.affected == 1, "SQLite 参数化插入");
            Expect(inserted.last_insert_id == 1, "SQLite last_insert_id");

            auto inserted_null = exec("INSERT INTO users(id,name) VALUES(2, NULL)");
            Expect(inserted_null.error.code == 0, "SQLite 插入 NULL");

            std::vector<std::vector<Cell>> rows;
            auto selected = connection->Execute(
                "SELECT id,name,score,data,note FROM users ORDER BY id", {}, options,
                [&rows](const RowBatch& batch) {
                    for (const auto& row : batch.rows) rows.push_back(row);
                    return true;
                });
            Expect(selected.error.code == 0 && selected.rows == 2, "SQLite SELECT 行数");
            Expect(rows.size() == 2, "SQLite 流式批次收集");
            if (rows.size() == 2) {
                Expect(rows[0][1].text == "alice", "SQLite 文本值");
                Expect(rows[0][2].text == "12.5", "SQLite DECIMAL 按数值格式化");
                Expect(rows[0][3].kind == ValueKind::Binary && rows[0][3].text == "AQID",
                       "SQLite BLOB → base64");
                Expect(rows[0][4].is_null, "SQLite NULL");
                Expect(rows[1][1].is_null, "SQLite 第二行 NULL");
            }

            auto bad = exec("SELECT * FROM missing_table");
            Expect(bad.error.code != 0 && !bad.error.message.empty(), "SQLite 错误映射");

            // 取消：长递归 CTE 由另一线程中断
            std::thread canceller([&connection] {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                connection->Cancel();
            });
            auto cancelled = exec(
                "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x < 1000000000) "
                "SELECT count(*) FROM c");
            canceller.join();
            Expect(cancelled.cancelled || cancelled.error.code != 0, "SQLite 取消查询");

            // 只读连接拒绝写语句
            namespace fs = std::filesystem;
            const fs::path file = fs::temp_directory_path() / "silkit_readonly_test.db";
            std::error_code ec;
            fs::remove(file, ec);
            ConnectParams write_params = params;
            write_params.file = Utf8Path(file);
            auto writable = driver.Open(write_params, error);
            Expect(writable != nullptr, "SQLite 打开临时文件");
            if (writable) {
                writable->Execute("CREATE TABLE t(id INTEGER)", {}, options, nullptr);
            }
            writable.reset();
            ConnectParams readonly_params = write_params;
            readonly_params.read_only = true;
            auto readonly = driver.Open(readonly_params, error);
            Expect(readonly != nullptr, "SQLite 只读打开");
            if (readonly) {
                auto denied = readonly->Execute("UPDATE t SET id = 2", {}, options, nullptr);
                Expect(denied.error.code == SQLITE_READONLY, "SQLite 只读连接拒绝写");
            }
            readonly.reset();
            fs::remove(file, ec);
        }
    }

    // ---------------------------------------------------------------- Session / SessionManager
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "silkit_session_test";
        std::error_code ec;
        fs::create_directories(dir, ec);

        silkit::session::SessionManager manager;
        silkit::db::ConnectParams params;
        params.engine = silkit::db::Engine::Sqlite;
        params.file = Utf8Path(dir / "session.db");

        silkit::session::OpenOutcome outcome;
        silkit::db::DbError error;
        Expect(manager.Open(params, outcome, error), "SessionManager 打开连接");
        Expect(!outcome.session_id.empty(), "SessionManager 返回 sessionId");
        Expect(outcome.server_version.find("3.") != std::string::npos, "SQLite 版本");

        auto session = manager.Find(outcome.session_id);
        Expect(session != nullptr, "SessionManager 查找会话");
        if (session) {
            const nlohmann::json result = session->RunSync([&session]() -> nlohmann::json {
                silkit::db::ExecOptions options;
                const auto executed =
                    session->Conn().Execute("SELECT 42 AS answer", {}, options, nullptr);
                return nlohmann::json{{"rows", executed.rows}, {"code", executed.error.code}};
            });
            Expect(result["rows"] == 1 && result["code"] == 0, "Session 串行执行");
        }
        Expect(manager.Close(outcome.session_id), "SessionManager 关闭连接");
        fs::remove_all(dir, ec);
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
                    Utf8Path(silkit::Logger::Instance().CurrentFile()).c_str());
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
