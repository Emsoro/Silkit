// SQLite 驱动 / Session 增强回归测试：只读切换、参数校验、多语句、init_sql、
// journal_mode 白名单、事务/元数据状态与重连。零依赖，CTest 直接运行。
#include "db/drivers/sqlite_driver.h"
#include "db/session_manager.h"
#include "db/sql_util.h"
#include "db/types.h"

#include <sqlite3.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

int g_failed = 0;
int g_total = 0;

// C++20 起 path::u8string() 返回 std::u8string(char8_t)，ConnectParams.file 是 std::string。
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

nlohmann::json EmptyJson() { return nlohmann::json::object(); }

}  // namespace

int main() {
    using namespace silkit::db;

    std::printf("Silkit sqlite driver tests\n");

    // ---------------------------------------------------------------- 只读语句分类
    {
        Expect(IsReadOnlyStatement("WITH c AS (SELECT 1) SELECT * FROM c"), "只读：WITH ... SELECT");
        Expect(!IsReadOnlyStatement("WITH c AS (SELECT 1) INSERT INTO t SELECT * FROM c"),
               "写：WITH ... INSERT 不能绕过只读拦截");
        Expect(!IsReadOnlyStatement("WITH c AS (SELECT 1) DELETE FROM t"), "写：WITH ... DELETE");
        Expect(IsReadOnlyStatement("PRAGMA table_xinfo('t')"), "只读：查询型 PRAGMA");
        Expect(!IsReadOnlyStatement("PRAGMA journal_mode = WAL"), "写：赋值型 PRAGMA");
        Expect(IsReadOnlyStatement("-- INSERT 只是注释\n SELECT 1"), "注释中的写关键字不误判");
        Expect(IsReadOnlyStatement("WITH c AS (SELECT 1) /* UPDATE */ SELECT * FROM c"),
               "WITH 语句注释中的写关键字不误判");
    }

    // ---------------------------------------------------------------- journal_mode 白名单
    {
        SqliteDriver driver;
        ConnectParams params;
        params.file = ":memory:";
        params.journal_mode = "WAL; DROP TABLE t";
        DbError err;
        auto connection = driver.Open(params, err);
        Expect(connection == nullptr, "journal_mode 非法值被拒绝");
        Expect(err.message.find("journal_mode") != std::string::npos, "journal_mode 错误信息可读");

        params.journal_mode = "wal";
        err = DbError{};
        auto wal = driver.Open(params, err);
        Expect(wal != nullptr, "journal_mode 大小写不敏感且白名单内允许");
    }

    // ---------------------------------------------------------------- init_sql / 参数 / 多语句
    {
        SqliteDriver driver;
        ConnectParams params;
        params.file = ":memory:";
        params.init_sql = "CREATE TABLE init_probe(x INTEGER);";
        DbError err;
        auto connection = driver.Open(params, err);
        Expect(connection != nullptr, "init_sql 执行后连接可用");
        if (!connection) return 1;

        ExecOptions opt;
        int probes = 0;
        auto probe = connection->Execute(
            "SELECT count(*) FROM sqlite_master WHERE name = 'init_probe'", {}, opt,
            [&probes](const RowBatch& batch) {
                if (!batch.rows.empty() && !batch.rows.front().empty()) {
                    probes = std::atoi(batch.rows.front().front().text.c_str());
                }
                return true;
            });
        Expect(probe.error.code == 0 && probes == 1, "init_sql 建的表确实存在");

        const std::vector<Param> one{Param{ValueKind::Text, false, "alice"}};
        auto mismatch = connection->Execute("SELECT ?, ?", one, opt, nullptr);
        Expect(mismatch.error.code != 0, "参数个数不足被拒绝");
        Expect(mismatch.error.message.find("参数个数不匹配") != std::string::npos,
               "参数不匹配错误可读");

        auto multi = connection->Execute("SELECT 1; SELECT 2", {}, opt, nullptr);
        Expect(multi.error.code != 0, "多语句被拒绝");
        Expect(multi.error.message.find("一次 Execute 只允许一条 SQL") != std::string::npos,
               "多语句错误提示拆分方式");

        auto trailing_ok = connection->Execute("SELECT 1; -- trailing comment", {}, opt, nullptr);
        Expect(trailing_ok.error.code == 0, "尾部分号与注释不算多语句");

        params.init_sql = "SELECT * FROM missing_table";
        DbError init_err;
        Expect(driver.Open(params, init_err) == nullptr, "init_sql 失败时不交付连接");
        Expect(init_err.message.find("初始 SQL") != std::string::npos, "init_sql 错误信息可读");
    }

    // ---------------------------------------------------------------- 运行时只读切换
    {
        SqliteDriver driver;
        ConnectParams params;
        params.file = ":memory:";
        DbError err;
        auto connection = driver.Open(params, err);
        Expect(connection != nullptr, "可写连接打开");
        if (!connection) return 1;
        ExecOptions opt;
        Expect(connection->Execute("CREATE TABLE t(x INTEGER)", {}, opt, nullptr).error.code == 0,
               "只读测试建表");
        connection->SetReadOnly(true);
        Expect(connection->IsReadOnly(), "连接切换到只读");
        auto denied = connection->Execute("INSERT INTO t VALUES(1)", {}, opt, nullptr);
        Expect(denied.error.code == SQLITE_READONLY, "运行时只读拦截写语句");
        connection->SetReadOnly(false);
        Expect(!connection->IsReadOnly() &&
                   connection->Execute("INSERT INTO t VALUES(1)", {}, opt, nullptr).error.code == 0,
               "恢复可写后可写入");
    }

    // ---------------------------------------------------------------- Session 状态与重连
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "silkit_reconnect_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);

        ConnectParams params;
        params.file = Utf8Path(dir / "grid.db");
        silkit::session::SessionManager manager;
        silkit::session::OpenOutcome outcome;
        DbError err;
        Expect(manager.Open(params, outcome, err), "SessionManager 打开");
        auto first = manager.Find(outcome.session_id);
        Expect(first != nullptr, "会话可查找");
        if (!first) return 1;

        first->RunSync([&first]() -> nlohmann::json {
            ExecOptions opt;
            first->Conn().Execute("CREATE TABLE t(x INTEGER)", {}, opt, nullptr);
            first->Conn().Execute("INSERT INTO t VALUES(7)", {}, opt, nullptr);
            return EmptyJson();
        });

        Expect(first->MetadataEpoch() == 0, "元数据代际初值为 0");
        Expect(first->InvalidateMetadata() == 1, "元数据代际递增");
        first->SetCurrentDatabase("main");
        Expect(first->CurrentDatabase() == "main", "会话当前库可设置");
        first->SetTx(silkit::session::TxState::InTransaction);
        Expect(std::string(silkit::session::TxStateName(first->Tx())) == "in_transaction",
               "事务状态可设置");

        // 只读打开的会话不能切回可写。
        std::string ro_error;
        const bool reopened = first->RunSync([&first, &ro_error]() -> nlohmann::json {
            return nlohmann::json{{"ok", first->SetReadOnly(true, ro_error)}};
        })["ok"].get<bool>();
        Expect(reopened && first->IsReadOnly(), "会话可切换为只读");
        Expect(first->RunSync([&first]() -> nlohmann::json {
                   std::string error;
                   return nlohmann::json{{"ok", first->SetReadOnly(false, error)}};
               })["ok"].get<bool>(),
               "可写打开的会话可以恢复可写");

        silkit::session::OpenOutcome reconnected;
        DbError reconn_err;
        Expect(manager.Reconnect(outcome.session_id, params, reconnected, reconn_err), "重连成功");
        Expect(reconnected.session_id == outcome.session_id, "重连保持 sessionId 不变");
        auto second = manager.Find(outcome.session_id);
        Expect(second != nullptr && second != first, "重连后为新 Session 对象");
        Expect(second && second->Tx() == silkit::session::TxState::Autocommit,
               "重连后事务状态重置");
        if (second) {
            int value = 0;
            second->RunSync([&second, &value]() -> nlohmann::json {
                ExecOptions opt;
                second->Conn().Execute("SELECT x FROM t", {}, opt,
                                      [&value](const RowBatch& batch) {
                                          if (!batch.rows.empty() && !batch.rows.front().empty()) {
                                              value = std::atoi(batch.rows.front().front().text.c_str());
                                          }
                                          return true;
                                      });
                return EmptyJson();
            });
            Expect(value == 7, "重连后数据仍可读取");
        }

        DbError failed_err;
        silkit::session::OpenOutcome ignored;
        ConnectParams bad = params;
        bad.file.clear();
        Expect(!manager.Reconnect(outcome.session_id, bad, ignored, failed_err),
               "重连失败时返回 false");
        Expect(manager.Find(outcome.session_id) != nullptr, "重连失败不破坏原会话");

        manager.Shutdown();
        fs::remove_all(dir, ec);
    }

    // ---------------------------------------------------------------- 只读会话不可解禁
    {
        namespace fs = std::filesystem;
        const fs::path file = fs::temp_directory_path() / "silkit_session_ro.db";
        std::error_code ec;
        fs::remove(file, ec);
        ConnectParams writable;
        writable.file = Utf8Path(file);
        DbError err;
        SqliteDriver driver;
        auto seed = driver.Open(writable, err);
        if (seed) {
            ExecOptions opt;
            seed->Execute("CREATE TABLE t(x)", {}, opt, nullptr);
        }
        ConnectParams ro = writable;
        ro.read_only = true;
        silkit::session::SessionManager manager;
        silkit::session::OpenOutcome outcome;
        DbError open_err;
        Expect(manager.Open(ro, outcome, open_err), "只读会话打开");
        auto session = manager.Find(outcome.session_id);
        std::string error;
        const bool ok = session->RunSync([&session, &error]() -> nlohmann::json {
            return nlohmann::json{{"ok", session->SetReadOnly(false, error)}};
        })["ok"].get<bool>();
        Expect(!ok, "只读打开的会话不能切回可写");
        Expect(error.find("只读模式打开") != std::string::npos, "只读冲突提示可读");
        manager.Shutdown();
        fs::remove(file, ec);
    }

    if (g_failed == 0) {
        std::printf("  %d checks passed\n", g_total);
        return 0;
    }
    std::printf("  %d/%d checks FAILED\n", g_failed, g_total);
    return 1;
}
