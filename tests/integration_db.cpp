// M2/M3 集成冒烟：仅在设置 DSN 环境变量时连接真实服务器；未设置时跳过。
// MySQL:   SILKIT_MYSQL_HOST/PORT/USER/PASSWORD/DATABASE
// MariaDB: 同一组变量（libmariadb）
// PG:      SILKIT_PG_HOST/PORT/USER/PASSWORD/DATABASE/SCHEMA
#include "db/driver_registry.h"
#include "db/identifier.h"
#include "db/table_ops.h"
#include "db/types.h"
#include "util/base64.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int g_failed = 0;

std::string Env(const char* key, const std::string& fallback = {}) {
    const char* value = std::getenv(key);
    return value ? value : fallback;
}

using silkit::db::Connection;
using silkit::db::Engine;
using silkit::db::ExecResult;
using silkit::db::Param;
using silkit::db::TableValue;
using silkit::db::ValueKind;

ExecResult Exec(Connection& connection, const std::string& sql,
                const std::vector<Param>& params = {}) {
    silkit::db::ExecOptions options;
    options.fetch_size = 10;
    options.max_cell_bytes = 0;
    return connection.Execute(sql, params, options, nullptr);
}

std::string TextValue(Connection& connection, const std::string& sql) {
    std::string out;
    silkit::db::ExecOptions options;
    options.fetch_size = 10;
    options.max_cell_bytes = 0;
    auto result = connection.Execute(sql, {}, options,
                                     [&out](const silkit::db::RowBatch& batch) {
                                         if (out.empty() && !batch.rows.empty() &&
                                             !batch.rows.front().empty() &&
                                             !batch.rows.front().front().is_null) {
                                             out = batch.rows.front().front().text;
                                         }
                                         return true;
                                     });
    (void)result;
    return out;
}

/// 写路径冒烟：仅当 SILKIT_INTEGRATION_WRITE=1 时执行，避免误改真实库。
void WriteProbe(Connection& connection, const silkit::db::ConnectParams& params,
                const char* label) {
    if (Env("SILKIT_INTEGRATION_WRITE") != "1") {
        std::printf("  [SKIP] %s：未设置 SILKIT_INTEGRATION_WRITE=1，跳过写路径\n", label);
        return;
    }

    const Engine engine = connection.EngineKind();
    const std::string table = "silkit_m5_probe";
    const std::string scope = engine == Engine::MySql
                                  ? (params.database.empty() ? connection.CurrentDatabase()
                                                             : params.database)
                                  : (params.schema.empty() ? std::string("public") : params.schema);
    const std::string qualified =
        silkit::db::QuoteIdentifier(engine, scope) + "." + silkit::db::QuoteIdentifier(engine, table);

    auto fail = [label](const std::string& message) {
        ++g_failed;
        std::printf("  [FAIL] %s 写路径：%s\n", label, message.c_str());
    };
    const int before = g_failed;

    Exec(connection, "DROP TABLE IF EXISTS " + qualified);

    const std::string create_sql =
        engine == Engine::MySql
            ? "CREATE TABLE " + qualified +
                  " (id BIGINT PRIMARY KEY, name VARCHAR(64) NOT NULL,"
                  " amount DECIMAL(10,2) NULL, avatar VARBINARY(16) NULL)"
            : "CREATE TABLE " + qualified +
                  " (id BIGINT PRIMARY KEY, name VARCHAR(64) NOT NULL,"
                  " amount NUMERIC(10,2) NULL, avatar BYTEA NULL)";
    auto created = Exec(connection, create_sql);
    if (created.error.code != 0) {
        fail("建表失败: " + created.error.message);
        return;
    }

    silkit::db::TableRef ref;
    ref.kind = "table";
    ref.name = table;
    ref.database = scope;
    ref.schema = scope;

    silkit::db::TableLayout layout;
    silkit::db::DbError layout_error;
    if (!silkit::db::LoadTableLayout(connection, ref, false, layout, layout_error)) {
        fail("加载布局失败: " + layout_error.message);
        Exec(connection, "DROP TABLE IF EXISTS " + qualified);
        return;
    }
    if (layout.identity.type != "pk" || !layout.editable) {
        fail("行标识/可编辑状态不符合预期: " + layout.identity.type);
        Exec(connection, "DROP TABLE IF EXISTS " + qualified);
        return;
    }

    const std::string binary = silkit::util::Base64Encode("\x00\x01\xFF", 3);
    silkit::db::TableChanges changes;
    silkit::db::InsertSpec insert;
    insert.values["id"] = TableValue{false, "101"};
    insert.values["name"] = TableValue{false, "silkit 🎉"};
    insert.values["amount"] = TableValue{false, "12.34"};
    insert.values["avatar"] = TableValue{false, binary};
    changes.inserts.push_back(insert);

    silkit::db::UpdateSpec update;
    update.keys["id"] = TableValue{false, "101"};
    update.changes["name"] = TableValue{false, "silkit 🎉 updated"};
    update.changes["amount"] = TableValue{true, {}};
    changes.updates.push_back(update);

    std::vector<silkit::db::PreparedChange> prepared;
    std::string build_error;
    if (!silkit::db::BuildChanges(connection, layout, changes, prepared, build_error)) {
        fail("生成语句失败: " + build_error);
        Exec(connection, "DROP TABLE IF EXISTS " + qualified);
        return;
    }
    if (prepared.size() != 2) {
        fail("语句数量不符合预期");
        Exec(connection, "DROP TABLE IF EXISTS " + qualified);
        return;
    }

    for (const auto& statement : prepared) {
        auto result = Exec(connection, statement.sql, statement.params);
        if (result.error.code != 0) {
            fail(statement.kind + " 执行失败: " + result.error.message + " · " + statement.sql);
            Exec(connection, "DROP TABLE IF EXISTS " + qualified);
            return;
        }
    }

    // 重复主键：参数化语句应返回错误而不是崩溃。
    auto duplicate = Exec(connection, prepared[0].sql, prepared[0].params);
    if (duplicate.error.code == 0) fail("重复主键未被拒绝");

    const std::string name = TextValue(connection, "SELECT name FROM " + qualified +
                                                       " WHERE id = 101");
    if (name != "silkit 🎉 updated") fail("emoji/更新回读不一致: " + name);
    const std::string amount_null =
        TextValue(connection, "SELECT CASE WHEN amount IS NULL THEN '1' ELSE '0' END FROM " +
                                  qualified + " WHERE id = 101");
    if (amount_null != "1") fail("DECIMAL 列置 NULL 失败");

    silkit::db::TableChanges remove_changes;
    silkit::db::DeleteSpec remove;
    remove.keys["id"] = TableValue{false, "101"};
    remove_changes.deletes.push_back(remove);
    std::vector<silkit::db::PreparedChange> remove_sql;
    if (!silkit::db::BuildChanges(connection, layout, remove_changes, remove_sql, build_error)) {
        fail("生成删除语句失败: " + build_error);
    } else if (Exec(connection, remove_sql[0].sql, remove_sql[0].params).error.code != 0) {
        fail("删除失败");
    }
    if (TextValue(connection, "SELECT COUNT(*) FROM " + qualified) != "0") {
        fail("删除后仍存在行");
    }

    Exec(connection, "DROP TABLE IF EXISTS " + qualified);
    if (g_failed == before) {
        std::printf("  [OK]   %s table_ops 写路径（插入/更新/NULL/emoji/删除）\n", label);
    }
}

void Probe(silkit::db::Engine engine, const char* prefix, const char* default_port,
           const char* default_user, const char* default_database, const char* label) {
    const std::string host_key = std::string(prefix) + "_HOST";
    const std::string host = Env(host_key.c_str());
    if (host.empty()) {
        std::printf("  [SKIP] %s：未设置 %s\n", label, host_key.c_str());
        return;
    }
    silkit::db::ConnectParams params;
    params.engine = engine;
    params.host = host;
    params.port = Env((std::string(prefix) + "_PORT").c_str(), default_port);
    params.user = Env((std::string(prefix) + "_USER").c_str(), default_user);
    params.password = Env((std::string(prefix) + "_PASSWORD").c_str());
    params.database = Env((std::string(prefix) + "_DATABASE").c_str(), default_database);
    params.schema = Env((std::string(prefix) + "_SCHEMA").c_str());
    params.connect_timeout_ms = 5000;

    silkit::db::Driver* driver = silkit::db::DriverRegistry::Instance().Find(engine);
    if (!driver) {
        ++g_failed;
        std::printf("  [FAIL] %s：驱动未注册\n", label);
        return;
    }
    silkit::db::DbError error;
    auto connection = driver->Open(params, error);
    if (!connection) {
        ++g_failed;
        std::printf("  [FAIL] %s 连接失败：%s (%s)\n", label, error.message.c_str(),
                    error.sql_state.c_str());
        return;
    }

    silkit::db::DbError ping_error;
    if (!connection->Ping(ping_error)) {
        ++g_failed;
        std::printf("  [FAIL] %s ping：%s\n", label, ping_error.message.c_str());
    }

    std::uint64_t seen = 0;
    silkit::db::ExecOptions options;
    options.fetch_size = 10;
    const silkit::db::ExecResult result = connection->Execute(
        "SELECT 1 AS one", {}, options, [&seen](const silkit::db::RowBatch& batch) {
            seen += batch.rows.size();
            return true;
        });
    if (result.error.code != 0 || seen == 0) {
        ++g_failed;
        std::printf("  [FAIL] %s SELECT 1：code=%d rows=%llu\n", label,
                    result.error.code, static_cast<unsigned long long>(seen));
    } else {
        std::printf("  [OK]   %s v%s · thread=%llu · db=%s · placeholder=%s\n",
                    label, connection->ServerVersion().c_str(),
                    static_cast<unsigned long long>(connection->ThreadId()),
                    connection->CurrentDatabase().c_str(),
                    connection->Placeholder(1).c_str());
    }

    WriteProbe(*connection, params, label);
}

}  // namespace

int main() {
    std::printf("Silkit M2/M3 integration smoke\n");
    silkit::db::RegisterBuiltinDrivers();
    Probe(silkit::db::Engine::MySql, "SILKIT_MYSQL", "3306", "root", "", "MySQL/MariaDB");
    Probe(silkit::db::Engine::Postgres, "SILKIT_PG", "5432", "postgres", "postgres", "PostgreSQL");
    std::printf("%d failures\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
