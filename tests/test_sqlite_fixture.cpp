// M6 SQLite 文件库夹具测试：校验 data/silkit_test.db 的元数据与数据，
// 并在临时副本上跑设计器 DDL，覆盖文件库（非内存）读写路径。
// 夹具由 tools/make_test_db.py 生成。
#include "db/ddl_builder.h"
#include "db/drivers/sqlite_driver.h"
#include "db/types.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#ifndef SILKIT_TEST_DATA_DIR
#define SILKIT_TEST_DATA_DIR "data"
#endif

namespace {

using silkit::db::Cell;
using silkit::db::Connection;
using silkit::db::DdlColumn;
using silkit::db::DdlMode;
using silkit::db::DdlPlan;
using silkit::db::DdlTableDefinition;
using silkit::db::Engine;
using silkit::db::ExecOptions;
using silkit::db::RowBatch;

int g_failed = 0;
int g_total = 0;

void Expect(bool condition, const std::string& name) {
    ++g_total;
    if (condition) return;
    ++g_failed;
    std::printf("  [FAIL] %s\n", name.c_str());
}

struct QueryResult {
    bool ok = false;
    std::string error;
    std::vector<std::vector<Cell>> rows;
};

QueryResult Run(Connection& connection, const std::string& sql) {
    QueryResult out;
    ExecOptions options;
    options.fetch_size = 100;
    options.max_cell_bytes = 0;
    auto result = connection.Execute(sql, {}, options, [&out](const RowBatch& batch) {
        for (const auto& row : batch.rows) out.rows.push_back(row);
        return true;
    });
    out.ok = result.error.code == 0;
    out.error = result.error.message;
    return out;
}

std::string Text(const std::vector<Cell>& row, std::size_t index) {
    if (index >= row.size() || row[index].is_null) return {};
    return row[index].text;
}

std::string Scalar(Connection& connection, const std::string& sql) {
    auto result = Run(connection, sql);
    if (!result.ok || result.rows.empty()) return {};
    return Text(result.rows.front(), 0);
}

bool Exec(Connection& connection, const std::string& sql) {
    auto result = Run(connection, sql);
    if (!result.ok) std::printf("  [SQL 失败] %s: %s\n", sql.c_str(), result.error.c_str());
    return result.ok;
}

std::unique_ptr<Connection> Open(const std::string& file, bool read_only,
                                 std::string& error) {
    silkit::db::SqliteDriver driver;
    silkit::db::ConnectParams params;
    params.engine = Engine::Sqlite;
    params.file = file;
    params.read_only = read_only;
    silkit::db::DbError db_error;
    auto connection = driver.Open(params, db_error);
    if (!connection) error = db_error.message;
    return connection;
}

std::filesystem::path FixturePath() {
    return std::filesystem::path(SILKIT_TEST_DATA_DIR) / "silkit_test.db";
}

const DdlColumn* FindColumn(const DdlTableDefinition& definition, const std::string& name) {
    for (const auto& column : definition.columns) {
        if (column.name == name) return &column;
    }
    return nullptr;
}

const silkit::db::DdlIndex* FindIndex(const DdlTableDefinition& definition,
                                      const std::string& name) {
    for (const auto& index : definition.indexes) {
        if (index.name == name) return &index;
    }
    return nullptr;
}

bool ExecPlan(Connection& connection, const DdlPlan& plan) {
    for (const auto& statement : plan.statements) {
        if (!Exec(connection, statement)) return false;
    }
    return true;
}

void TestFixtureReadOnly() {
    const auto fixture = FixturePath();
    Expect(std::filesystem::exists(fixture), "夹具存在: " + fixture.string());
    if (!std::filesystem::exists(fixture)) return;

    std::string error;
    auto connection = Open(fixture.string(), true, error);
    Expect(connection != nullptr, "只读打开夹具" + (error.empty() ? "" : ": " + error));
    if (!connection) return;

    auto tables = Run(*connection,
                      "SELECT name FROM sqlite_master WHERE type = 'table'"
                      " AND name NOT LIKE 'sqlite_%' ORDER BY name");
    Expect(tables.ok, "读取表清单");
    Expect(tables.rows.size() == 3, "夹具包含 3 张表");
    if (tables.rows.size() == 3) {
        Expect(Text(tables.rows[0], 0) == "orders", "表清单第 1 项为 orders");
        Expect(Text(tables.rows[1], 0) == "products", "表清单第 2 项为 products");
        Expect(Text(tables.rows[2], 0) == "users", "表清单第 3 项为 users");
    }

    auto views = Run(*connection,
                     "SELECT name FROM sqlite_master WHERE type = 'view' AND name = 'active_users'");
    Expect(views.ok && views.rows.size() == 1, "夹具包含视图 active_users");
    auto triggers = Run(*connection,
                        "SELECT name FROM sqlite_master WHERE type = 'trigger'"
                        " AND name = 'trg_users_touch'");
    Expect(triggers.ok && triggers.rows.size() == 1, "夹具包含触发器 trg_users_touch");

    Expect(Scalar(*connection, "SELECT COUNT(*) FROM users") == "12", "users 12 行");
    Expect(Scalar(*connection, "SELECT COUNT(*) FROM orders") == "8", "orders 8 行");
    Expect(Scalar(*connection, "SELECT COUNT(*) FROM products") == "6", "products 6 行");
    Expect(Scalar(*connection, "SELECT COUNT(*) FROM active_users") == "8",
           "active_users 视图 8 行");
    Expect(Scalar(*connection, "SELECT COUNT(*) FROM users u JOIN orders o"
                               " ON o.user_id = u.id") == "8",
           "users/orders 关联 8 行");
    Expect(Scalar(*connection, "SELECT name FROM users WHERE id = 1") == "林知夏",
           "users 首行数据正确");
}

void TestFixtureMetadata() {
    std::string error;
    auto connection = Open(FixturePath().string(), true, error);
    if (!connection) {
        Expect(false, "元数据测试打开夹具: " + error);
        return;
    }

    DdlTableDefinition users;
    silkit::db::DbError load_error;
    Expect(silkit::db::LoadDdlDefinition(*connection, "main", "users", users, load_error),
           "加载 users 定义: " + load_error.message);
    Expect(users.columns.size() == 8, "users 8 列");
    Expect(users.indexes.size() == 2, "users 2 个索引");
    Expect(users.foreign_keys.empty(), "users 无外键");

    const DdlColumn* id = FindColumn(users, "id");
    Expect(id != nullptr && id->primary_key && id->auto_increment, "users.id 主键自增");
    Expect(id != nullptr && id->type == "INTEGER", "users.id 类型 INTEGER");
    const DdlColumn* email = FindColumn(users, "email");
    Expect(email != nullptr && !email->not_null, "users.email 可空");
    const DdlColumn* status = FindColumn(users, "status");
    Expect(status != nullptr && status->not_null, "users.status NOT NULL");
    Expect(status != nullptr && status->has_default, "users.status 有默认值");
    Expect(status != nullptr && status->default_expr.find("active") != std::string::npos,
           "users.status 默认值为 active");
    const DdlColumn* avatar = FindColumn(users, "avatar");
    Expect(avatar != nullptr && avatar->type.find("BLOB") != std::string::npos,
           "users.avatar 为 BLOB");

    const auto* email_index = FindIndex(users, "idx_users_email");
    Expect(email_index != nullptr && email_index->unique, "idx_users_email 唯一索引");
    const auto* status_index = FindIndex(users, "idx_users_status");
    Expect(status_index != nullptr && !status_index->unique, "idx_users_status 普通索引");

    DdlTableDefinition orders;
    Expect(silkit::db::LoadDdlDefinition(*connection, "main", "orders", orders, load_error),
           "加载 orders 定义: " + load_error.message);
    Expect(orders.foreign_keys.size() == 1, "orders 1 个外键");
    if (orders.foreign_keys.size() == 1) {
        const auto& fk = orders.foreign_keys.front();
        Expect(fk.ref_table == "users", "外键指向 users");
        Expect(fk.ref_columns.size() == 1 && fk.ref_columns[0] == "id", "外键引用 users.id");
        Expect(fk.columns.size() == 1 && fk.columns[0] == "user_id", "外键列为 user_id");
        Expect(fk.on_delete == "CASCADE", "外键 ON DELETE CASCADE");
    }

    DdlTableDefinition products;
    Expect(silkit::db::LoadDdlDefinition(*connection, "main", "products", products, load_error),
           "加载 products 定义: " + load_error.message);
    Expect(products.columns.size() == 5, "products 5 列");
}

void TestReadOnlyWriteBlocked() {
    std::string error;
    auto connection = Open(FixturePath().string(), true, error);
    if (!connection) {
        Expect(false, "只读写入测试打开夹具: " + error);
        return;
    }
    auto result = Run(*connection, "CREATE TABLE fixture_write_probe (id INTEGER)");
    Expect(!result.ok, "只读连接拒绝 CREATE TABLE");
    Expect(Scalar(*connection, "SELECT COUNT(*) FROM sqlite_master"
                               " WHERE name = 'fixture_write_probe'") == "0",
           "只读写入未生效");
}

void TestDdlOnCopy() {
    const auto fixture = FixturePath();
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temp = std::filesystem::temp_directory_path() /
                      ("silkit_fixture_" + std::to_string(stamp) + ".db");
    std::error_code copy_error;
    std::filesystem::copy_file(fixture, temp,
                               std::filesystem::copy_options::overwrite_existing, copy_error);
    Expect(!copy_error, "复制夹具到临时文件");
    if (copy_error) return;

    auto cleanup = [&temp]() {
        std::error_code ignore;
        std::filesystem::remove(temp, ignore);
        for (const char* suffix : {"-wal", "-shm", "-journal"}) {
            std::filesystem::remove(temp.string() + suffix, ignore);
        }
    };

    std::string error;
    auto connection = Open(temp.string(), false, error);
    Expect(connection != nullptr, "读写打开临时夹具" + (error.empty() ? "" : ": " + error));
    if (!connection) {
        cleanup();
        return;
    }

    // 夹具表 ALTER：新增列后数据保留。
    DdlTableDefinition users;
    silkit::db::DbError load_error;
    if (!silkit::db::LoadDdlDefinition(*connection, "main", "users", users, load_error)) {
        Expect(false, "临时库加载 users: " + load_error.message);
        cleanup();
        return;
    }
    DdlTableDefinition users_updated = users;
    DdlColumn nickname;
    nickname.name = "nickname";
    nickname.type = "TEXT";
    users_updated.columns.push_back(nickname);
    DdlPlan alter_users;
    Expect(silkit::db::BuildDdlPlan(DdlMode::Alter, Engine::Sqlite, "main", users, users_updated,
                                    "", alter_users, error),
           "生成 users ALTER 计划: " + error);
    Expect(ExecPlan(*connection, alter_users), "执行 users ALTER 计划");
    DdlTableDefinition users_reloaded;
    Expect(silkit::db::LoadDdlDefinition(*connection, "main", "users", users_reloaded, load_error),
           "ALTER 后重新加载 users");
    Expect(FindColumn(users_reloaded, "nickname") != nullptr, "users.nickname 已存在");
    Expect(Scalar(*connection, "SELECT COUNT(*) FROM users") == "12", "ALTER 后 users 数据保留");

    // 新建表 → 插入 → 列重命名 → 清空 → 删除。
    DdlTableDefinition notes;
    notes.engine = Engine::Sqlite;
    notes.scope = "main";
    notes.name = "notes";
    DdlColumn note_id;
    note_id.name = "id";
    note_id.type = "INTEGER";
    note_id.not_null = true;
    note_id.primary_key = true;
    note_id.auto_increment = true;
    DdlColumn title;
    title.name = "title";
    title.type = "TEXT";
    title.not_null = true;
    notes.columns = {note_id, title};

    DdlPlan create;
    Expect(silkit::db::BuildDdlPlan(DdlMode::Create, Engine::Sqlite, "main", {}, notes, "",
                                    create, error),
           "生成 notes 建表计划: " + error);
    Expect(ExecPlan(*connection, create), "执行 notes 建表计划");
    Expect(Exec(*connection, "INSERT INTO notes (title) VALUES ('夹具测试')"), "notes 插入数据");
    Expect(Scalar(*connection, "SELECT COUNT(*) FROM notes") == "1", "notes 1 行");

    DdlTableDefinition notes_loaded;
    Expect(silkit::db::LoadDdlDefinition(*connection, "main", "notes", notes_loaded, load_error),
           "加载 notes 定义: " + load_error.message);
    Expect(notes_loaded.columns.size() == 2, "notes 2 列");
    Expect(notes_loaded.columns[0].auto_increment, "notes.id 自增");

    DdlTableDefinition notes_updated = notes_loaded;
    notes_updated.columns[1].name = "heading";
    notes_updated.columns[1].old_name = "title";
    DdlColumn body;
    body.name = "body";
    body.type = "TEXT";
    notes_updated.columns.push_back(body);
    DdlPlan alter_notes;
    Expect(silkit::db::BuildDdlPlan(DdlMode::Alter, Engine::Sqlite, "main", notes_loaded,
                                    notes_updated, "", alter_notes, error),
           "生成 notes ALTER 计划: " + error);
    Expect(ExecPlan(*connection, alter_notes), "执行 notes ALTER 计划");
    Expect(Scalar(*connection, "SELECT COUNT(*) FROM notes WHERE heading = '夹具测试'") == "1",
           "列重命名后数据保留");
    DdlTableDefinition notes_reloaded;
    Expect(silkit::db::LoadDdlDefinition(*connection, "main", "notes", notes_reloaded, load_error),
           "ALTER 后重新加载 notes");
    Expect(FindColumn(notes_reloaded, "body") != nullptr, "notes.body 已添加");
    Expect(FindColumn(notes_reloaded, "heading") != nullptr, "notes.heading 已存在");

    DdlPlan truncate;
    Expect(silkit::db::BuildDdlPlan(DdlMode::Truncate, Engine::Sqlite, "main", {}, notes_updated,
                                    "", truncate, error),
           "生成 notes 清空计划: " + error);
    Expect(ExecPlan(*connection, truncate), "执行 notes 清空计划");
    Expect(Scalar(*connection, "SELECT COUNT(*) FROM notes") == "0", "notes 清空后 0 行");

    DdlPlan drop;
    Expect(silkit::db::BuildDdlPlan(DdlMode::Drop, Engine::Sqlite, "main", {}, notes_updated, "",
                                    drop, error),
           "生成 notes 删除计划: " + error);
    Expect(ExecPlan(*connection, drop), "执行 notes 删除计划");
    auto after_drop = Run(*connection, "SELECT COUNT(*) FROM notes");
    Expect(!after_drop.ok, "notes 删除后不可查询");

    connection.reset();
    cleanup();
}

}  // namespace

int main() {
    const auto fixture = FixturePath();
    std::printf("sqlite fixture tests (%s)\n", fixture.string().c_str());

    TestFixtureReadOnly();
    TestFixtureMetadata();
    TestReadOnlyWriteBlocked();
    TestDdlOnCopy();

    if (g_failed == 0) {
        std::printf("  [PASS] %d checks\n", g_total);
        return 0;
    }
    std::printf("  [FAILED] %d/%d checks\n", g_failed, g_total);
    return 1;
}
