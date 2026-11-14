// M5 表格数据编辑核心测试：行标识探测、分页/排序/筛选/搜索、CRUD 语句生成、
// NULL/emoji/小数/二进制参数。全部基于 SQLite 内存库，零外部依赖。
#include "db/drivers/sqlite_driver.h"
#include "db/table_ops.h"
#include "db/types.h"
#include "util/base64.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_failed = 0;
int g_total = 0;

void Expect(bool condition, const std::string& name) {
    ++g_total;
    if (condition) return;
    ++g_failed;
    std::printf("  [FAIL] %s\n", name.c_str());
}

void ExpectString(const std::string& actual, const std::string& expected,
                  const std::string& name) {
    Expect(actual == expected, name + "（实际: " + actual + "）");
}

using silkit::db::Cell;
using silkit::db::Connection;
using silkit::db::ExecOptions;
using silkit::db::Param;
using silkit::db::RowBatch;
using silkit::db::TableLayout;
using silkit::db::TableRef;
using silkit::db::TableValue;
using silkit::db::ValueKind;

struct QueryResult {
    bool ok = false;
    std::string error;
    std::vector<std::vector<Cell>> rows;
};

QueryResult Run(Connection& connection, const std::string& sql,
                const std::vector<Param>& params = {}) {
    QueryResult out;
    ExecOptions options;
    options.fetch_size = 100;
    options.max_cell_bytes = 0;
    auto result = connection.Execute(sql, params, options, [&out](const RowBatch& batch) {
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

bool Exec(Connection& connection, const std::string& sql) {
    return Run(connection, sql).ok;
}

TableRef Ref(const std::string& name, const std::string& kind = "table") {
    TableRef ref;
    ref.kind = kind;
    ref.name = name;
    ref.database = "main";
    return ref;
}

Param TextParam(const std::string& text) {
    Param param;
    param.kind = ValueKind::Text;
    param.text = text;
    return param;
}

TableValue Value(const std::string& text) {
    TableValue value;
    value.text = text;
    return value;
}

std::unique_ptr<Connection> OpenMemory() {
    silkit::db::SqliteDriver driver;
    silkit::db::ConnectParams params;
    params.file = ":memory:";
    silkit::db::DbError error;
    auto connection = driver.Open(params, error);
    if (!connection) std::printf("  [FATAL] 打开 SQLite 失败: %s\n", error.message.c_str());
    return connection;
}

void TestPrimaryKeyLayout(Connection& connection) {
    Expect(Exec(connection,
                "CREATE TABLE users("
                " id INTEGER PRIMARY KEY,"
                " name TEXT NOT NULL,"
                " email TEXT,"
                " balance DECIMAL(10,2),"
                " avatar BLOB,"
                " created_at TEXT)"),
           "建表 users");
    Expect(Exec(connection,
                "INSERT INTO users(id, name, email, balance, avatar, created_at) VALUES"
                "(1, 'Alice', 'alice@example.com', '12.34', X'00FF', '2026-01-01 10:00:00'),"
                "(2, 'Bob', NULL, '0.00', NULL, '2026-01-02 10:00:00'),"
                "(3, '张三 🎉', 'zhang@example.com', '99.99', NULL, NULL)"),
           "插入 users 数据");

    TableLayout layout;
    silkit::db::DbError error;
    Expect(silkit::db::LoadTableLayout(connection, Ref("users"), false, layout, error),
           "users 加载布局");
    ExpectString(layout.identity.type, "pk", "users 行标识为 PK");
    Expect(layout.identity.columns.size() == 1 && layout.identity.columns[0] == "id",
           "users 主键列为 id");
    Expect(layout.editable, "users 可编辑");
    Expect(layout.columns.size() == 6, "users 列数量");
    Expect(layout.columns[0].meta.auto_increment, "INTEGER 主键标记为自增");

    // 分页 + hasMore
    silkit::db::RowsQuery query;
    query.page = 0;
    query.page_size = 2;
    silkit::db::BuiltQuery built;
    std::string build_error;
    Expect(silkit::db::BuildRowsQuery(connection, layout, query, built, build_error),
           "构建分页查询");
    auto page0 = Run(connection, built.sql, built.params);
    Expect(page0.ok, "执行分页查询");
    Expect(page0.rows.size() == 3, "pageSize+1 探测 hasMore");
    Expect(page0.rows.size() > 0 && Text(page0.rows[0], 0) == "1", "第一页首行 id");
    Expect(page0.rows.size() > 2 && Text(page0.rows[2], 1) == "张三 🎉", "emoji 文本原样返回");

    // 排序 + 第二页
    query.page = 1;
    query.order.column = "id";
    query.order.desc = true;
    Expect(silkit::db::BuildRowsQuery(connection, layout, query, built, build_error),
           "构建倒序第二页");
    Expect(built.sql.find("ORDER BY \"id\" DESC") != std::string::npos, "排序片段");
    auto page1 = Run(connection, built.sql, built.params);
    Expect(page1.ok && page1.rows.size() == 1, "倒序第二页只剩 1 行");
    Expect(page1.rows.size() > 0 && Text(page1.rows[0], 0) == "1", "倒序分页边界");

    // 搜索（大小写不敏感在 SQLite 中仅对 ASCII 生效）
    query.page = 0;
    query.order = {};
    query.search = "ali";
    std::string search_error;
    Expect(silkit::db::BuildRowsQuery(connection, layout, query, built, search_error),
           "构建搜索");
    auto searched = Run(connection, built.sql, built.params);
    Expect(searched.ok && searched.rows.size() == 1, "搜索命中 1 行");
    Expect(searched.rows.size() > 0 && Text(searched.rows[0], 1) == "Alice", "搜索命中 Alice");

    // 筛选：balance > 10
    query.search.clear();
    silkit::db::TableFilter filter;
    filter.column = "balance";
    filter.op = ">";
    filter.value = "10";
    query.filters = {filter};
    std::string filter_error;
    Expect(silkit::db::BuildRowsQuery(connection, layout, query, built, filter_error),
           "构建数值筛选");
    auto filtered = Run(connection, built.sql, built.params);
    Expect(filtered.ok && filtered.rows.size() == 2, "balance > 10 命中 2 行");

    // 精确计数
    silkit::db::BuiltQuery count;
    std::string count_error;
    Expect(silkit::db::BuildCountQuery(connection, layout, {}, count, count_error),
           "构建计数查询");
    auto counted = Run(connection, count.sql, count.params);
    Expect(counted.ok && counted.rows.size() == 1 &&
               std::atoi(Text(counted.rows[0], 0).c_str()) == 3,
           "COUNT(*) 为 3");

    // 估算（SQLite 精确 COUNT）
    std::int64_t total = -1;
    std::string estimate_error;
    Expect(silkit::db::EstimateTotalRows(connection, layout, {}, total, estimate_error) &&
               total == 3,
           "SQLite 估算总数等于精确值");
}

void TestUniqueIdentity(Connection& connection) {
    Expect(Exec(connection, "CREATE TABLE uq(code TEXT NOT NULL UNIQUE, val TEXT)"),
           "建表 uq");
    TableLayout layout;
    silkit::db::DbError error;
    Expect(silkit::db::LoadTableLayout(connection, Ref("uq"), false, layout, error),
           "uq 加载布局");
    ExpectString(layout.identity.type, "unique", "uq 使用唯一索引定位");
    Expect(layout.identity.columns.size() == 1 && layout.identity.columns[0] == "code",
           "uq 唯一列 code");
    Expect(layout.editable, "uq 可编辑");

    // 可空唯一索引不能用于定位：NULL 会有多行
    Expect(Exec(connection, "CREATE TABLE uq_null(code TEXT UNIQUE, val TEXT)"),
           "建表 uq_null");
    TableLayout nullable;
    Expect(silkit::db::LoadTableLayout(connection, Ref("uq_null"), false, nullable, error),
           "uq_null 加载布局");
    ExpectString(nullable.identity.type, "rowid", "可空唯一索引退化为 rowid");
}

void TestRowIdIdentity(Connection& connection) {
    Expect(Exec(connection, "CREATE TABLE logs(msg TEXT, at TEXT)"), "建表 logs");
    Expect(Exec(connection, "INSERT INTO logs(msg, at) VALUES('a', '1'), ('b', '2')"),
           "插入 logs");
    TableLayout layout;
    silkit::db::DbError error;
    Expect(silkit::db::LoadTableLayout(connection, Ref("logs"), false, layout, error),
           "logs 加载布局");
    ExpectString(layout.identity.type, "rowid", "logs 使用 rowid");
    Expect(layout.editable, "logs 可编辑");

    silkit::db::RowsQuery query;
    query.page_size = 10;
    silkit::db::BuiltQuery built;
    std::string build_error;
    Expect(silkit::db::BuildRowsQuery(connection, layout, query, built, build_error),
           "构建 rowid 查询");
    Expect(built.sql.rfind("SELECT rowid, ", 0) == 0, "rowid 前置到 SELECT 列表");
    auto rows = Run(connection, built.sql, built.params);
    Expect(rows.ok && rows.rows.size() == 2, "rowid 查询返回 2 行");
    Expect(rows.rows.size() > 0 && Text(rows.rows[0], 0) == "1", "首行 rowid 为 1");

    std::map<std::string, TableValue> keys{{"rowid", Value("2")}};
    std::string where;
    std::vector<Param> params;
    std::string where_error;
    Expect(silkit::db::BuildIdentityWhere(connection, layout, keys, where, params, where_error),
           "构建 rowid WHERE");
    ExpectString(where, "rowid = ?", "rowid WHERE 使用裸 rowid");
    auto located = Run(connection, "SELECT msg FROM " + layout.qualified + " WHERE " + where,
                       params);
    Expect(located.ok && located.rows.size() == 1 && Text(located.rows[0], 0) == "b",
           "rowid 定位到第二行");}

void TestReadOnlyView(Connection& connection) {
    Expect(Exec(connection, "CREATE VIEW v_users AS SELECT id, name FROM users"),
           "建视图 v_users");
    TableLayout layout;
    silkit::db::DbError error;
    Expect(silkit::db::LoadTableLayout(connection, Ref("v_users", "view"), false, layout, error),
           "视图加载布局");
    ExpectString(layout.identity.type, "none", "视图没有行标识");
    Expect(!layout.editable, "视图只读");
    Expect(!layout.read_only_reason.empty(), "视图给出只读原因");

    silkit::db::TableChanges changes;
    silkit::db::InsertSpec insert;
    insert.values["name"] = Value("x");
    changes.inserts.push_back(insert);
    std::vector<silkit::db::PreparedChange> prepared;
    std::string build_error;
    Expect(!silkit::db::BuildChanges(connection, layout, changes, prepared, build_error),
           "只读视图拒绝写入");
    Expect(!build_error.empty(), "只读写入给出原因");
}

void TestCompositePrimaryKey(Connection& connection) {
    Expect(Exec(connection,
                "CREATE TABLE pair(a TEXT NOT NULL, b INTEGER NOT NULL, v TEXT,"
                " PRIMARY KEY(a, b))"),
           "建表 pair");
    TableLayout layout;
    silkit::db::DbError error;
    Expect(silkit::db::LoadTableLayout(connection, Ref("pair"), false, layout, error),
           "pair 加载布局");
    ExpectString(layout.identity.type, "pk", "pair 使用复合主键");
    Expect(layout.identity.columns.size() == 2 && layout.identity.columns[0] == "a" &&
               layout.identity.columns[1] == "b",
           "复合主键列顺序");

    std::map<std::string, TableValue> keys{{"a", Value("x")}, {"b", Value("2")}};
    std::string where;
    std::vector<Param> params;
    std::string where_error;
    Expect(silkit::db::BuildIdentityWhere(connection, layout, keys, where, params, where_error),
           "复合主键 WHERE");
    ExpectString(where, "\"a\" = ? AND \"b\" = ?", "复合主键 WHERE 片段");
    Expect(params.size() == 2, "复合主键参数个数");

    // 主键优先于唯一索引
    Expect(Exec(connection,
                "CREATE TABLE both(id INTEGER PRIMARY KEY, code TEXT NOT NULL UNIQUE)"),
           "建表 both");
    TableLayout both;
    Expect(silkit::db::LoadTableLayout(connection, Ref("both"), false, both, error),
           "both 加载布局");
    ExpectString(both.identity.type, "pk", "PK 优先于唯一索引");
}

void TestGeneratedColumn(Connection& connection) {
    Expect(Exec(connection,
                "CREATE TABLE gen(a INTEGER, b INTEGER GENERATED ALWAYS AS (a * 2) VIRTUAL)"),
           "建表 gen");
    TableLayout layout;
    silkit::db::DbError error;
    Expect(silkit::db::LoadTableLayout(connection, Ref("gen"), false, layout, error),
           "gen 加载布局");
    const silkit::db::TableColumn* generated = silkit::db::FindColumn(layout, "b");
    Expect(generated != nullptr && generated->meta.generated, "b 标记为生成列");

    silkit::db::TableChanges changes;
    silkit::db::InsertSpec insert;
    insert.values["b"] = Value("1");
    changes.inserts.push_back(insert);
    std::vector<silkit::db::PreparedChange> prepared;
    std::string build_error;
    Expect(!silkit::db::BuildChanges(connection, layout, changes, prepared, build_error),
           "生成列禁止写入");
    Expect(build_error.find("生成列") != std::string::npos, "生成列错误可读");
}

void TestBuildChanges(Connection& connection) {
    TableLayout layout;
    silkit::db::DbError error;
    Expect(silkit::db::LoadTableLayout(connection, Ref("users"), false, layout, error),
           "users 重新加载布局");

    const std::string binary = silkit::util::Base64Encode("\x01\x02", 2);

    silkit::db::TableChanges changes;
    silkit::db::InsertSpec insert;
    insert.values["name"] = Value("新用户 🎉");
    insert.values["email"] = TableValue{true, {}};
    insert.values["balance"] = Value("123.45");
    TableValue blob;
    blob.text = binary;
    insert.values["avatar"] = blob;
    insert.client_index = 7;
    changes.inserts.push_back(insert);

    silkit::db::UpdateSpec update;
    update.keys["id"] = Value("1");
    update.changes["name"] = Value("Alice 2");
    update.changes["email"] = TableValue{true, {}};
    update.client_index = 8;
    changes.updates.push_back(update);

    silkit::db::DeleteSpec remove;
    remove.keys["id"] = Value("2");
    remove.client_index = 9;
    changes.deletes.push_back(remove);

    std::vector<silkit::db::PreparedChange> prepared;
    std::string build_error;
    Expect(silkit::db::BuildChanges(connection, layout, changes, prepared, build_error),
           "构建 CRUD 语句");
    Expect(prepared.size() == 3, "插入/更新/删除各一条");

    const auto& insert_sql = prepared[0];
    ExpectString(insert_sql.kind, "insert", "第一条为 insert");
    Expect(insert_sql.client_index == 7, "insert 回传 clientIndex");
    Expect(insert_sql.sql.find("INSERT INTO \"main\".\"users\"") == 0, "insert 表名引用");
    Expect(insert_sql.params.size() == 4, "insert 参数个数");
    bool has_null = false;
    bool has_binary = false;
    for (const auto& param : insert_sql.params) {
        if (param.is_null) has_null = true;
        if (param.kind == ValueKind::Binary) has_binary = true;
    }
    Expect(has_null, "NULL 使用参数 is_null");
    Expect(has_binary, "BLOB 参数标记为 Binary");

    const auto& update_sql = prepared[1];
    ExpectString(update_sql.kind, "update", "第二条为 update");
    Expect(update_sql.sql.find("UPDATE \"main\".\"users\" SET") == 0, "update SQL 前缀");
    Expect(update_sql.sql.find("WHERE \"id\" = ?") != std::string::npos, "update WHERE");
    Expect(update_sql.params.size() == 3, "update 参数个数（2 值 + 1 键）");
    Expect(update_sql.params[0].is_null, "update 设置 NULL（email 按列名排序在前）");
    ExpectString(update_sql.params[1].text, "Alice 2", "update 第二个参数");
    ExpectString(update_sql.params[2].text, "1", "update 键值");

    const auto& delete_sql = prepared[2];
    ExpectString(delete_sql.kind, "delete", "第三条为 delete");
    ExpectString(delete_sql.sql, "DELETE FROM \"main\".\"users\" WHERE \"id\" = ?",
                 "delete SQL 片段");
    Expect(delete_sql.params.size() == 1 && delete_sql.params[0].text == "2", "delete 参数");

    // 实际执行三引擎共用的参数化语句（SQLite）
    for (const auto& statement : prepared) {
        auto result = Run(connection, statement.sql, statement.params);
        Expect(result.ok, "执行生成语句: " + statement.kind + " " + result.error);
    }

    auto verify = Run(connection,
                      "SELECT name FROM users WHERE id = 1", {});
    Expect(verify.ok && verify.rows.size() == 1, "更新后行仍存在");
    Expect(verify.rows.size() > 0 && Text(verify.rows[0], 0) == "Alice 2", "更新后的 name");
    auto null_check = Run(connection, "SELECT email IS NULL FROM users WHERE id = 1", {});
    Expect(null_check.ok && null_check.rows.size() > 0 &&
               Text(null_check.rows[0], 0) == "1",
           "email 实际为 NULL");
    auto inserted = Run(connection,
                        "SELECT COUNT(*) FROM users WHERE name = '新用户 🎉'", {});
    Expect(inserted.ok && inserted.rows.size() > 0 &&
               Text(inserted.rows[0], 0) == "1",
           "emoji 行插入成功");
    auto blob_check = Run(connection, "SELECT avatar FROM users WHERE name = '新用户 🎉'", {});
    Expect(blob_check.ok && blob_check.rows.size() > 0 && !blob_check.rows[0][0].is_null &&
               blob_check.rows[0][0].text == binary,
           "BLOB 原样写入并读取");
}

void TestNullKeysRejected(Connection& connection) {
    TableLayout layout;
    silkit::db::DbError error;
    Expect(silkit::db::LoadTableLayout(connection, Ref("users"), false, layout, error),
           "users 布局（NULL 键）");
    std::map<std::string, TableValue> keys{{"id", TableValue{true, {}}}};
    std::string where;
    std::vector<Param> params;
    std::string where_error;
    Expect(!silkit::db::BuildIdentityWhere(connection, layout, keys, where, params, where_error),
           "NULL 主键值被拒绝");
    Expect(where_error.find("id") != std::string::npos, "NULL 主键错误包含列名");
}

void TestReadOnlySession(Connection& connection) {
    TableLayout layout;
    silkit::db::DbError error;
    Expect(silkit::db::LoadTableLayout(connection, Ref("users"), true, layout, error),
           "只读会话加载 users");
    Expect(!layout.editable, "只读会话下不可编辑");
    Expect(layout.read_only_reason.find("只读") != std::string::npos, "只读原因可读");
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("Silkit table ops tests\n");
    auto connection = OpenMemory();
    if (!connection) return 1;

    TestPrimaryKeyLayout(*connection);
    TestUniqueIdentity(*connection);
    TestRowIdIdentity(*connection);
    TestReadOnlyView(*connection);
    TestCompositePrimaryKey(*connection);
    TestGeneratedColumn(*connection);
    TestBuildChanges(*connection);
    TestNullKeysRejected(*connection);
    TestReadOnlySession(*connection);

    std::printf("  %d/%d 通过\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
