// M2/M3 集成冒烟：仅在设置 DSN 环境变量时连接真实服务器；未设置时跳过。
// MySQL:   SILKIT_MYSQL_HOST/PORT/USER/PASSWORD/DATABASE
// MariaDB: 同一组变量（libmariadb）
// PG:      SILKIT_PG_HOST/PORT/USER/PASSWORD/DATABASE/SCHEMA
#include "db/ddl_builder.h"
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

/// M6 DDL 写路径：建父/子表→加载→ALTER→重命名→清空→删除，以及建库/删库。
void DdlProbe(Connection& connection, const silkit::db::ConnectParams& params, const char* label) {
    if (Env("SILKIT_INTEGRATION_WRITE") != "1") {
        std::printf("  [SKIP] %s：未设置 SILKIT_INTEGRATION_WRITE=1，跳过 DDL 路径\n", label);
        return;
    }

    using silkit::db::DdlColumn;
    using silkit::db::DdlForeignKey;
    using silkit::db::DdlIndex;
    using silkit::db::DdlMode;
    using silkit::db::DdlPlan;
    using silkit::db::DdlTableDefinition;

    const Engine engine = connection.EngineKind();
    const bool mysql = engine == Engine::MySql;
    const std::string scope = mysql
                                  ? (params.database.empty() ? connection.CurrentDatabase()
                                                             : params.database)
                                  : (params.schema.empty() ? std::string("public")
                                                           : params.schema);
    const std::string parent = "silkit_m6_parent";
    const std::string child = "silkit_m6_probe";
    const std::string renamed = "silkit_m6_probe_renamed";

    auto qualified = [&](const std::string& name) {
        return silkit::db::QuoteIdentifier(engine, scope) + "." +
               silkit::db::QuoteIdentifier(engine, name);
    };
    auto fail = [label](const std::string& message) {
        ++g_failed;
        std::printf("  [FAIL] %s DDL：%s\n", label, message.c_str());
    };
    auto column = [](std::string name, std::string type, bool not_null, bool primary = false,
                     bool auto_increment = false) {
        DdlColumn out;
        out.name = std::move(name);
        out.type = std::move(type);
        out.not_null = not_null;
        out.primary_key = primary;
        out.auto_increment = auto_increment;
        return out;
    };
    auto exec_plan = [&](const DdlPlan& plan, const std::string& what) {
        for (const auto& statement : plan.statements) {
            auto result = Exec(connection, statement);
            if (result.error.code != 0) {
                fail(what + "执行失败: " + result.error.message + " · " + statement);
                return false;
            }
        }
        return true;
    };
    auto build = [&](DdlMode mode, const DdlTableDefinition& original,
                     const DdlTableDefinition& updated, const std::string& new_name,
                     DdlPlan& plan) {
        std::string error;
        if (!silkit::db::BuildDdlPlan(mode, engine, scope, original, updated, new_name, plan,
                                      error)) {
            fail("生成计划失败: " + error);
            return false;
        }
        return true;
    };

    const int before = g_failed;
    Exec(connection, "DROP TABLE IF EXISTS " + qualified(renamed));
    Exec(connection, "DROP TABLE IF EXISTS " + qualified(child));
    Exec(connection, "DROP TABLE IF EXISTS " + qualified(parent));

    DdlTableDefinition parent_def;
    parent_def.engine = engine;
    parent_def.scope = scope;
    parent_def.name = parent;
    parent_def.columns = {
        column("id", mysql ? "BIGINT" : "bigint", true, true, true),
        column("title", mysql ? "VARCHAR(64)" : "character varying(64)", true)};
    DdlPlan create_parent;
    if (!build(DdlMode::Create, {}, parent_def, "", create_parent)) return;
    if (!exec_plan(create_parent, "建父表")) return;

    DdlTableDefinition child_def;
    child_def.engine = engine;
    child_def.scope = scope;
    child_def.name = child;
    child_def.columns = {
        column("id", mysql ? "BIGINT" : "bigint", true, true, true),
        column("name", mysql ? "VARCHAR(64)" : "character varying(64)", true),
        column("parent_id", mysql ? "BIGINT" : "bigint", false),
        column("note", mysql ? "VARCHAR(255)" : "character varying(255)", false)};
    DdlIndex unique_name;
    unique_name.name = "uq_" + child + "_name";
    unique_name.unique = true;
    unique_name.columns = {"name"};
    child_def.indexes = {unique_name};
    DdlForeignKey fk;
    fk.name = "fk_" + child + "_parent";
    fk.columns = {"parent_id"};
    fk.ref_table = parent;
    fk.ref_columns = {"id"};
    fk.on_delete = "CASCADE";
    child_def.foreign_keys = {fk};
    DdlPlan create_child;
    if (!build(DdlMode::Create, {}, child_def, "", create_child)) return;
    if (!exec_plan(create_child, "建子表")) return;

    DdlTableDefinition loaded;
    silkit::db::DbError load_error;
    if (!silkit::db::LoadDdlDefinition(connection, scope, child, loaded, load_error)) {
        fail("加载子表定义失败: " + load_error.message);
        Exec(connection, "DROP TABLE IF EXISTS " + qualified(child));
        Exec(connection, "DROP TABLE IF EXISTS " + qualified(parent));
        return;
    }
    if (loaded.columns.size() != 4) {
        fail("加载列数不是 4（实际 " + std::to_string(loaded.columns.size()) + "）");
    }
    if (loaded.indexes.size() != 1) {
        fail("加载索引数不是 1（实际 " + std::to_string(loaded.indexes.size()) + "）");
    }
    if (loaded.foreign_keys.size() != 1) {
        fail("加载外键数不是 1（实际 " + std::to_string(loaded.foreign_keys.size()) + "）");
    }

    // ALTER：列重命名、改类型、加列、换索引、删外键与列。
    DdlTableDefinition updated = loaded;
    for (auto& item : updated.columns) {
        if (item.name == "note") {
            item.name = "remark";
            item.old_name = "note";
        }
        if (item.name == "name") {
            item.type = mysql ? "VARCHAR(128)" : "character varying(128)";
        }
    }
    DdlColumn score = column("score", mysql ? "INT" : "integer", false);
    score.has_default = true;
    score.default_expr = "0";
    updated.columns.push_back(score);
    DdlIndex idx_name;
    idx_name.name = "idx_" + child + "_name";
    idx_name.columns = {"name"};
    DdlIndex idx_remark;
    idx_remark.name = "idx_" + child + "_remark";
    idx_remark.columns = {"remark"};
    updated.indexes = {idx_name, idx_remark};
    updated.foreign_keys.clear();
    for (auto it = updated.columns.begin(); it != updated.columns.end(); ++it) {
        if (it->name == "parent_id") {
            updated.columns.erase(it);
            break;
        }
    }

    DdlPlan alter;
    if (!build(DdlMode::Alter, loaded, updated, "", alter)) return;
    if (!exec_plan(alter, "ALTER ")) return;

    DdlTableDefinition reloaded;
    if (!silkit::db::LoadDdlDefinition(connection, scope, child, reloaded, load_error)) {
        fail("ALTER 后加载失败: " + load_error.message);
        return;
    }
    if (reloaded.columns.size() != 4) fail("ALTER 后列数不是 4");
    if (!reloaded.foreign_keys.empty()) fail("ALTER 后外键未删除");
    if (reloaded.indexes.size() != 2) fail("ALTER 后索引数量不是 2");
    bool remark_found = false;
    bool type_ok = false;
    for (const auto& item : reloaded.columns) {
        if (item.name == "remark") remark_found = true;
        if (item.name == "name" && item.type.find("128") != std::string::npos) type_ok = true;
    }
    if (!remark_found) fail("列重命名未生效");
    if (!type_ok) fail("列类型修改未生效");

    DdlTableDefinition target = reloaded;
    target.name = renamed;
    DdlPlan rename;
    if (!build(DdlMode::Alter, reloaded, target, renamed, rename)) return;
    if (!exec_plan(rename, "表重命名")) return;
    if (Exec(connection, "SELECT COUNT(*) FROM " + qualified(renamed)).error.code != 0) {
        fail("重命名后的表不可查询");
    }

    auto inserted = Exec(connection, "INSERT INTO " + qualified(renamed) +
                                        " (name, score) VALUES ('ddl-probe', 1)");
    if (inserted.error.code != 0) fail("插入探针行失败: " + inserted.error.message);

    DdlTableDefinition truncate_target;
    truncate_target.engine = engine;
    truncate_target.scope = scope;
    truncate_target.name = renamed;
    DdlPlan truncate;
    if (build(DdlMode::Truncate, {}, truncate_target, "", truncate)) {
        if (exec_plan(truncate, "清空表") &&
            TextValue(connection, "SELECT COUNT(*) FROM " + qualified(renamed)) != "0") {
            fail("清空后行数不为 0");
        }
    }

    DdlPlan drop_child;
    if (build(DdlMode::Drop, {}, target, "", drop_child)) exec_plan(drop_child, "删除表");
    DdlPlan drop_parent;
    if (build(DdlMode::Drop, {}, parent_def, "", drop_parent)) exec_plan(drop_parent, "删除父表");

    // 建库/删库（MySQL/PG）。
    const std::string db_name = "silkit_m6_db_probe";
    Exec(connection, "DROP DATABASE IF EXISTS " + silkit::db::QuoteIdentifier(engine, db_name));
    DdlTableDefinition db_def;
    db_def.engine = engine;
    db_def.scope = scope;
    db_def.name = db_name;
    if (mysql) db_def.options.charset = "utf8mb4";
    DdlPlan create_db;
    if (build(DdlMode::CreateDatabase, {}, db_def, db_name, create_db) &&
        exec_plan(create_db, "建库")) {
        DdlPlan drop_db;
        if (build(DdlMode::DropDatabase, {}, db_def, db_name, drop_db)) {
            exec_plan(drop_db, "删库");
        }
    }

    if (g_failed == before) {
        std::printf("  [OK]   %s DDL 路径（建表/加载/ALTER/重命名/清空/删除/建库删库）\n", label);
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
    DdlProbe(*connection, params, label);
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
