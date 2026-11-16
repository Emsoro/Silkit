// 本地 SQLite 文件库端到端功能测试（不依赖 MySQL / PostgreSQL / UI）。
// 覆盖：索引与视图、分页/筛选/搜索、单元格编辑与应用更改（事务 + 回滚）、
// 五种导出中的 CSV/JSON/SQL，以及 CSV 导入建表与回读。
#include "db/drivers/sqlite_driver.h"
#include "db/table_ops.h"
#include "db/types.h"
#include "io/csv.h"
#include "io/exporter.h"
#include "io/importer.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

using silkit::db::Cell;
using silkit::db::Connection;
using silkit::db::ExecOptions;
using silkit::db::Param;
using silkit::db::RowBatch;
using silkit::db::TableChanges;
using silkit::db::TableFilter;
using silkit::db::TableLayout;
using silkit::db::TableOrder;
using silkit::db::TableRef;
using silkit::db::TableValue;

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

QueryResult Run(Connection& connection, const std::string& sql,
                const std::vector<Param>& params = {}) {
    QueryResult out;
    ExecOptions options;
    options.fetch_size = 200;
    options.max_cell_bytes = 0;
    auto result = connection.Execute(sql, params, options, [&out](const RowBatch& batch) {
        for (const auto& row : batch.rows) out.rows.push_back(row);
        return true;
    });
    out.ok = result.error.code == 0;
    out.error = result.error.message;
    return out;
}

bool Exec(Connection& connection, const std::string& sql) {
    const auto result = Run(connection, sql);
    if (!result.ok) std::printf("  [SQL 失败] %s: %s\n", sql.c_str(), result.error.c_str());
    return result.ok;
}

std::string Text(const std::vector<Cell>& row, std::size_t index) {
    if (index >= row.size() || row[index].is_null) return {};
    return row[index].text;
}

std::string Scalar(Connection& connection, const std::string& sql,
                   const std::vector<Param>& params = {}) {
    const auto result = Run(connection, sql, params);
    if (!result.ok || result.rows.empty()) return {};
    return Text(result.rows.front(), 0);
}

std::int64_t ScalarInt(Connection& connection, const std::string& sql,
                       const std::vector<Param>& params = {}) {
    const std::string text = Scalar(connection, sql, params);
    return text.empty() ? -1 : std::stoll(text);
}

TableRef Ref(const std::string& name, const std::string& kind = "table") {
    TableRef ref;
    ref.kind = kind;
    ref.name = name;
    ref.database = "main";
    return ref;
}

TableValue Value(const std::string& text) {
    TableValue value;
    value.text = text;
    return value;
}

std::unique_ptr<Connection> OpenFile(const std::filesystem::path& file, std::string& error) {
    silkit::db::SqliteDriver driver;
    silkit::db::ConnectParams params;
    params.engine = silkit::db::Engine::Sqlite;
    params.file = file.string();
    params.foreign_keys = true;
    params.journal_mode = "WAL";
    silkit::db::DbError db_error;
    auto connection = driver.Open(params, db_error);
    if (!connection) error = db_error.message;
    return connection;
}

std::filesystem::path TempDatabase() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("silkit_e2e_sqlite_" + std::to_string(stamp) + ".db");
}

void RemoveDatabase(const std::filesystem::path& file) {
    std::error_code ignore;
    std::filesystem::remove(file, ignore);
    for (const char* suffix : {"-wal", "-shm", "-journal"}) {
        std::filesystem::remove(file.string() + suffix, ignore);
    }
}

bool SeedSchema(Connection& connection) {
    const char* schema =
        "CREATE TABLE users ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " name TEXT NOT NULL,"
        " email TEXT,"
        " status TEXT NOT NULL DEFAULT 'active',"
        " balance DECIMAL(10,2),"
        " created_at TEXT);"
        "CREATE UNIQUE INDEX idx_users_email ON users(email);"
        "CREATE INDEX idx_users_status ON users(status);"
        "CREATE TABLE orders ("
        " id INTEGER PRIMARY KEY,"
        " user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
        " amount DECIMAL(10,2));"
        "CREATE INDEX idx_orders_user ON orders(user_id);"
        "CREATE VIEW active_users AS"
        " SELECT id, name, email FROM users WHERE status = 'active';"
        "INSERT INTO users(id, name, email, status, balance, created_at) VALUES"
        " (1, 'Alice', 'alice@example.com', 'active', 120.50, '2026-01-01'),"
        " (2, 'Bob', 'bob@example.com', 'active', 88.00, '2026-01-02'),"
        " (3, 'Carol', 'carol@example.com', 'disabled', 0.00, '2026-01-03'),"
        " (4, 'David', 'david@example.com', 'active', 42.25, '2026-01-04'),"
        " (5, 'Eva', 'eva@example.com', 'active', 75.10, '2026-01-05'),"
        " (6, 'Frank', 'frank@example.com', 'disabled', 13.40, '2026-01-06'),"
        " (7, 'Grace', 'grace@example.com', 'active', 9.99, '2026-01-07'),"
        " (8, 'Henry', 'henry@example.com', 'active', 200.00, '2026-01-08'),"
        " (9, 'Ivy', 'ivy@example.com', 'disabled', 5.00, '2026-01-09'),"
        " (10, 'Jack', 'jack@example.com', 'active', 310.00, '2026-01-10'),"
        " (11, '林知夏', 'lin@example.com', 'active', 66.60, '2026-01-11'),"
        " (12, '张三', NULL, 'disabled', 0.00, '2026-01-12');"
        "INSERT INTO orders(id, user_id, amount) VALUES"
        " (101, 1, 30.00), (102, 1, 45.50), (103, 2, 12.00), (104, 11, 99.00);";
    // 驱动一次只执行一条语句，按分号拆分后逐条执行。
    std::string script(schema);
    std::size_t start = 0;
    for (;;) {
        const std::size_t semicolon = script.find(';', start);
        if (semicolon == std::string::npos) break;
        const std::string statement = script.substr(start, semicolon - start + 1);
        start = semicolon + 1;
        if (!Exec(connection, statement)) return false;
    }
    return true;
}

// ---------------------------------------------------------------- 索引与视图
void TestIndexesAndViews(Connection& connection) {
    auto indexes = Run(connection, "PRAGMA index_list('users')");
    Expect(indexes.ok && indexes.rows.size() >= 2, "索引：users 至少 2 个索引");
    bool email_unique = false;
    bool status_plain = false;
    for (const auto& row : indexes.rows) {
        const std::string name = Text(row, 1);
        if (name == "idx_users_email") email_unique = Text(row, 2) == "1";
        if (name == "idx_users_status") status_plain = Text(row, 2) == "0";
    }
    Expect(email_unique, "索引：idx_users_email 唯一");
    Expect(status_plain, "索引：idx_users_status 普通");

    auto views = Run(connection,
                     "SELECT name FROM sqlite_master WHERE type = 'view' AND name = 'active_users'");
    Expect(views.ok && views.rows.size() == 1, "视图：active_users 出现在 sqlite_master");
    Expect(ScalarInt(connection, "SELECT COUNT(*) FROM active_users") == 8,
           "视图：active_users 初始 8 行");

    TableLayout users;
    silkit::db::DbError load_error;
    Expect(silkit::db::LoadTableLayout(connection, Ref("users"), false, users, load_error),
           "元数据：加载 users 布局: " + load_error.message);
    Expect(users.columns.size() == 6, "元数据：users 6 列");
    Expect(users.identity.type == "pk" && users.identity.columns.size() == 1 &&
               users.identity.columns[0] == "id",
           "元数据：users 以 id 主键标识");
    Expect(users.editable, "元数据：users 可编辑");

    TableLayout view;
    Expect(silkit::db::LoadTableLayout(connection, Ref("active_users", "view"), false, view,
                                       load_error),
           "元数据：加载 active_users 视图布局: " + load_error.message);
    Expect(!view.editable && !view.read_only_reason.empty(), "元数据：视图只读并给出原因");

    silkit::db::RowsQuery query;
    query.page_size = 10;
    silkit::db::BuiltQuery built;
    std::string error;
    Expect(silkit::db::BuildRowsQuery(connection, view, query, built, error),
           "视图：生成分页查询: " + error);
    auto rows = Run(connection, built.sql, built.params);
    Expect(rows.ok && rows.rows.size() == 8, "视图：分页查询返回 8 行");
}

// ---------------------------------------------------------------- 筛选与搜索
void TestFiltering(Connection& connection, const TableLayout& users) {
    std::string error;

    silkit::db::RowsQuery query;
    query.page_size = 3;
    TableFilter active;
    active.column = "status";
    active.op = "=";
    active.value = "active";
    query.filters.push_back(active);
    TableOrder order;
    order.column = "name";
    query.order = order;

    silkit::db::BuiltQuery built;
    Expect(silkit::db::BuildRowsQuery(connection, users, query, built, error),
           "筛选：生成 status=active 查询: " + error);
    auto rows = Run(connection, built.sql, built.params);
    // 查询会多取 1 行用于探测「是否有下一页」。
    Expect(rows.ok && rows.rows.size() == 4, "筛选：第一页 3+1 行");
    if (rows.rows.size() == 4) {
        Expect(Text(rows.rows[0], 1) == "Alice", "筛选：排序第 1 行为 Alice");
        Expect(Text(rows.rows[2], 1) == "David", "筛选：排序第 3 行为 David");
    }

    silkit::db::BuiltQuery count;
    Expect(silkit::db::BuildCountQuery(connection, users, query, count, error),
           "筛选：生成计数查询: " + error);
    Expect(ScalarInt(connection, count.sql, count.params) == 8, "筛选：status=active 共 8 行");

    query.page = 1;
    Expect(silkit::db::BuildRowsQuery(connection, users, query, built, error),
           "筛选：生成第二页查询: " + error);
    rows = Run(connection, built.sql, built.params);
    Expect(rows.ok && rows.rows.size() == 4 && Text(rows.rows[0], 1) == "Eva",
           "筛选：第二页首行为 Eva");

    // 数值比较
    silkit::db::RowsQuery balance_query;
    balance_query.page_size = 50;
    TableFilter rich;
    rich.column = "balance";
    rich.op = ">";
    rich.value = "50";
    balance_query.filters.push_back(rich);
    Expect(silkit::db::BuildCountQuery(connection, users, balance_query, count, error),
           "筛选：生成 balance>50 计数: " + error);
    Expect(ScalarInt(connection, count.sql, count.params) == 6, "筛选：balance>50 共 6 行");

    // IS NULL
    silkit::db::RowsQuery null_query;
    TableFilter no_email;
    no_email.column = "email";
    no_email.op = "IS NULL";
    null_query.filters.push_back(no_email);
    Expect(silkit::db::BuildRowsQuery(connection, users, null_query, built, error),
           "筛选：生成 email IS NULL 查询: " + error);
    rows = Run(connection, built.sql, built.params);
    Expect(rows.ok && rows.rows.size() == 1 && Text(rows.rows[0], 1) == "张三",
           "筛选：email IS NULL 命中张三");

    // 整表搜索
    silkit::db::RowsQuery search;
    search.search = "Grace";
    Expect(silkit::db::BuildRowsQuery(connection, users, search, built, error),
           "搜索：生成 Grace 查询: " + error);
    rows = Run(connection, built.sql, built.params);
    Expect(rows.ok && rows.rows.size() == 1 && Text(rows.rows[0], 1) == "Grace",
           "搜索：Grace 命中 1 行");

    search.search = "example.com";
    Expect(silkit::db::BuildCountQuery(connection, users, search, count, error),
           "搜索：生成 example.com 计数: " + error);
    Expect(ScalarInt(connection, count.sql, count.params) == 11,
           "搜索：11 行带 example.com 邮箱");
}

// ---------------------------------------------------------------- 编辑与应用
void TestEditing(Connection& connection, const TableLayout& users) {
    TableChanges changes;

    silkit::db::InsertSpec insert;
    insert.values["name"] = Value("E2E New");
    insert.values["email"] = Value("e2e@example.com");
    insert.values["status"] = Value("active");
    insert.values["balance"] = Value("12.34");
    insert.values["created_at"] = Value("2026-11-16");
    changes.inserts.push_back(insert);

    silkit::db::UpdateSpec update;
    update.keys["id"] = Value("2");
    update.changes["status"] = Value("disabled");
    changes.updates.push_back(update);

    silkit::db::DeleteSpec remove;
    remove.keys["id"] = Value("3");
    changes.deletes.push_back(remove);

    std::vector<silkit::db::PreparedChange> prepared;
    std::string error;
    Expect(silkit::db::BuildChanges(connection, users, changes, prepared, error),
           "编辑：生成 CRUD 语句: " + error);
    Expect(prepared.size() == 3, "编辑：共 3 条变更");
    Expect(prepared.size() == 3 && prepared[0].kind == "insert" && prepared[1].kind == "update" &&
               prepared[2].kind == "delete",
           "编辑：变更顺序 insert/update/delete");

    Expect(Exec(connection, "BEGIN;"), "应用：开启事务");
    bool applied = true;
    for (const auto& change : prepared) {
        ExecOptions options;
        options.fetch_size = 10;
        options.max_cell_bytes = 0;
        const auto result = connection.Execute(change.sql, change.params, options, nullptr);
        if (result.error.code != 0) {
            std::printf("  [应用失败] %s: %s\n", change.sql.c_str(),
                        result.error.message.c_str());
            applied = false;
            break;
        }
    }
    Expect(applied, "应用：三条变更执行成功");
    Expect(applied && Exec(connection, "COMMIT;"), "应用：提交事务");

    Expect(ScalarInt(connection, "SELECT COUNT(*) FROM users") == 12, "应用：总行数 12（+1-1）");
    Expect(Scalar(connection, "SELECT status FROM users WHERE id = 2") == "disabled",
           "应用：Bob 状态已更新");
    Expect(ScalarInt(connection, "SELECT COUNT(*) FROM users WHERE id = 3") == 0,
           "应用：Carol 已删除");
    Expect(Scalar(connection, "SELECT name FROM users WHERE email = 'e2e@example.com'") ==
               "E2E New",
           "应用：新行已插入");
    Expect(ScalarInt(connection, "SELECT COUNT(*) FROM active_users") == 8,
           "应用：视图随数据同步（8 个活跃用户）");

    // 编辑后回滚：不应影响库内数据。
    Expect(Exec(connection, "BEGIN;"), "回滚：开启事务");
    Expect(Exec(connection, "DELETE FROM users WHERE id = 4;"), "回滚：删除 David");
    Expect(Exec(connection, "ROLLBACK;"), "回滚：回滚事务");
    Expect(ScalarInt(connection, "SELECT COUNT(*) FROM users WHERE id = 4") == 1,
           "回滚：David 仍在");
}

// ---------------------------------------------------------------- 导出
void TestExport(Connection& connection, const TableLayout& users) {
    silkit::db::RowsQuery query;
    query.page_size = 2000;
    silkit::db::BuiltQuery built;
    std::string error;
    Expect(silkit::db::BuildRowsQuery(connection, users, query, built, error),
           "导出：生成整表查询: " + error);

    std::vector<silkit::db::ColumnMeta> columns;
    for (const auto& column : users.columns) columns.push_back(column.meta);

    auto stream_rows = [&](silkit::io::ExportWriter& writer) {
        ExecOptions options;
        options.fetch_size = 200;
        options.max_cell_bytes = 0;
        return connection.Execute(built.sql, built.params, options, [&writer](const RowBatch& batch) {
                   for (const auto& row : batch.rows) writer.WriteRow(row);
                   return true;
               }).error.code == 0;
    };

    {
        std::ostringstream out;
        silkit::io::ExportOptions options;
        options.format = silkit::io::ExportFormat::Csv;
        silkit::io::ExportWriter writer(out, options);
        writer.Begin(columns);
        Expect(stream_rows(writer), "导出 CSV：读取行");
        writer.Finish();
        const std::string text = out.str();
        Expect(writer.ok() && writer.rows() == 12, "导出 CSV：12 行");
        Expect(text.find("id,name,email,status,balance,created_at\r\n") == 0,
               "导出 CSV：表头顺序");
        Expect(text.find("E2E New") != std::string::npos, "导出 CSV：包含新增行");
        Expect(text.find("张三,,disabled") != std::string::npos, "导出 CSV：NULL 输出为空");
    }

    {
        std::ostringstream out;
        silkit::io::ExportOptions options;
        options.format = silkit::io::ExportFormat::Json;
        silkit::io::ExportWriter writer(out, options);
        writer.Begin(columns);
        Expect(stream_rows(writer), "导出 JSON：读取行");
        writer.Finish();
        const nlohmann::json parsed = nlohmann::json::parse(out.str(), nullptr, false);
        Expect(parsed.is_array() && parsed.size() == 12, "导出 JSON：12 条记录");
        bool found = false;
        for (const auto& item : parsed) {
            if (item.value("name", "") == "E2E New") found = item["email"] == "e2e@example.com";
            if (item["email"].is_null()) {
                Expect(item["name"] == "张三", "导出 JSON：NULL 邮箱保留为 null");
            }
        }
        Expect(found, "导出 JSON：新增行字段正确");
    }

    {
        std::ostringstream out;
        silkit::io::ExportOptions options;
        options.format = silkit::io::ExportFormat::Sql;
        options.table_name = "main.users";
        silkit::io::ExportWriter writer(out, options);
        writer.Begin(columns);
        Expect(stream_rows(writer), "导出 SQL：读取行");
        writer.Finish();
        const std::string text = out.str();
        Expect(text.find("INSERT INTO \"main\".\"users\"") != std::string::npos,
               "导出 SQL：限定表名");
        Expect(text.find("'E2E New'") != std::string::npos, "导出 SQL：文本字面量");
    }
}

// ---------------------------------------------------------------- 导入
void TestImport(Connection& connection) {
    const std::string csv =
        "id,name,email,status,balance\r\n"
        "1,导入一,i1@example.com,active,1.10\r\n"
        "2,导入二,i2@example.com,active,2.20\r\n"
        "3,导入三,i3@example.com,disabled,3.30\r\n";

    silkit::io::CsvImportOptions options;
    options.has_header = true;
    options.create_table = true;
    options.table = "imported_users";
    options.columns = {
        {0, "id", "INTEGER"},
        {1, "name", "TEXT"},
        {2, "email", "TEXT"},
        {3, "status", "TEXT"},
        {4, "balance", "DECIMAL(10,2)"},
    };

    silkit::io::CsvImportResult result;
    silkit::io::CsvImportError error;
    const auto status = silkit::io::ImportCsv(connection, csv, silkit::io::CsvOptions{}, options,
                                              result, error);
    Expect(status == silkit::io::ImportStatus::Ok,
           "导入：CSV 建表导入成功" + (error.message.empty() ? "" : ": " + error.message));
    Expect(result.rows == 3, "导入：返回 3 行");
    Expect(ScalarInt(connection, "SELECT COUNT(*) FROM imported_users") == 3, "导入：库中 3 行");
    Expect(Scalar(connection, "SELECT name FROM imported_users WHERE id = 2") == "导入二",
           "导入：中文内容正确");

    // 回读：导入表再导出为 CSV，行数与表头一致。
    TableLayout imported;
    silkit::db::DbError load_error;
    Expect(silkit::db::LoadTableLayout(connection, Ref("imported_users"), false, imported,
                                       load_error),
           "导入：加载 imported_users 布局: " + load_error.message);
    silkit::db::RowsQuery query;
    query.page_size = 100;
    silkit::db::BuiltQuery built;
    std::string build_error;
    Expect(silkit::db::BuildRowsQuery(connection, imported, query, built, build_error),
           "导入：生成回读查询: " + build_error);

    std::vector<silkit::db::ColumnMeta> columns;
    for (const auto& column : imported.columns) columns.push_back(column.meta);
    std::ostringstream out;
    silkit::io::ExportOptions export_options;
    export_options.format = silkit::io::ExportFormat::Csv;
    silkit::io::ExportWriter writer(out, export_options);
    writer.Begin(columns);
    ExecOptions exec_options;
    exec_options.fetch_size = 100;
    exec_options.max_cell_bytes = 0;
    const auto read = connection.Execute(built.sql, built.params, exec_options,
                                         [&writer](const RowBatch& batch) {
                                             for (const auto& row : batch.rows) writer.WriteRow(row);
                                             return true;
                                         });
    writer.Finish();
    Expect(read.error.code == 0 && writer.rows() == 3, "导入导出：回读 3 行");
    Expect(out.str().find("id,name,email,status,balance\r\n") == 0, "导入导出：回读表头");
    Expect(out.str().find("导入三") != std::string::npos, "导入导出：回读中文内容");
}

}  // namespace

int main() {
    const auto file = TempDatabase();
    std::printf("sqlite e2e workflow (%s)\n", file.string().c_str());

    std::string error;
    auto connection = OpenFile(file, error);
    Expect(connection != nullptr, "打开本地 SQLite 文件库" + (error.empty() ? "" : ": " + error));
    if (connection) {
        Expect(SeedSchema(*connection), "初始化 schema 与数据");

        TableLayout users;
        silkit::db::DbError load_error;
        Expect(silkit::db::LoadTableLayout(*connection, Ref("users"), false, users, load_error),
               "加载 users 布局: " + load_error.message);

        TestIndexesAndViews(*connection);
        TestFiltering(*connection, users);
        TestEditing(*connection, users);
        TestExport(*connection, users);
        TestImport(*connection);
        connection.reset();
    }
    RemoveDatabase(file);

    if (g_failed == 0) {
        std::printf("  [PASS] %d checks\n", g_total);
        return 0;
    }
    std::printf("  [FAILED] %d/%d checks\n", g_failed, g_total);
    return 1;
}
