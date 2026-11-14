// M6 DDL 生成器测试：SQLite 建表→改表→删表 roundtrip，MySQL/PostgreSQL SQL 片段，
// 令牌失效与参数校验。SQLite 部分使用内存库，其余为纯字符串断言。
#include "db/ddl_builder.h"
#include "db/drivers/sqlite_driver.h"
#include "db/types.h"

#include <cstdio>
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

void ExpectContains(const std::string& actual, const std::string& needle,
                    const std::string& name) {
    Expect(actual.find(needle) != std::string::npos,
           name + "（未找到: " + needle + "；实际: " + actual + "）");
}

void ExpectNotContains(const std::string& actual, const std::string& needle,
                       const std::string& name) {
    Expect(actual.find(needle) == std::string::npos,
           name + "（不应出现: " + needle + "；实际: " + actual + "）");
}

using silkit::db::Cell;
using silkit::db::Connection;
using silkit::db::DdlColumn;
using silkit::db::DdlForeignKey;
using silkit::db::DdlIndex;
using silkit::db::DdlMode;
using silkit::db::DdlPlan;
using silkit::db::DdlTableDefinition;
using silkit::db::ExecOptions;
using silkit::db::RowBatch;

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

bool Exec(Connection& connection, const std::string& sql) {
    auto result = Run(connection, sql);
    if (!result.ok) std::printf("  [SQL 失败] %s: %s\n", sql.c_str(), result.error.c_str());
    return result.ok;
}

bool ExecPlan(Connection& connection, const DdlPlan& plan) {
    for (const auto& statement : plan.statements) {
        if (!Exec(connection, statement)) return false;
    }
    return true;
}

std::string JoinPlan(const DdlPlan& plan) {
    std::string sql;
    for (const auto& statement : plan.statements) {
        if (!sql.empty()) sql += "\n";
        sql += statement;
    }
    return sql;
}

bool HasWarning(const DdlPlan& plan, const std::string& needle) {
    for (const auto& warning : plan.warnings) {
        if (warning.find(needle) != std::string::npos) return true;
    }
    return false;
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

DdlColumn Column(const std::string& name, const std::string& type, bool not_null = false,
                 bool primary_key = false, bool auto_increment = false) {
    DdlColumn column;
    column.name = name;
    column.type = type;
    column.not_null = not_null;
    column.primary_key = primary_key;
    column.auto_increment = auto_increment;
    return column;
}

DdlIndex Index(const std::string& name, const std::vector<std::string>& columns,
               bool unique = false) {
    DdlIndex index;
    index.name = name;
    index.columns = columns;
    index.unique = unique;
    return index;
}

DdlForeignKey ForeignKey(const std::string& name, const std::vector<std::string>& columns,
                         const std::string& ref_table,
                         const std::vector<std::string>& ref_columns,
                         const std::string& on_delete = "NO ACTION") {
    DdlForeignKey fk;
    fk.name = name;
    fk.columns = columns;
    fk.ref_table = ref_table;
    fk.ref_columns = ref_columns;
    fk.on_delete = on_delete;
    return fk;
}

DdlTableDefinition SqliteUsers() {
    DdlTableDefinition definition;
    definition.engine = silkit::db::Engine::Sqlite;
    definition.scope = "main";
    definition.name = "users";
    definition.columns = {Column("id", "INTEGER", false, true, true),
                          Column("name", "TEXT", true),
                          Column("email", "TEXT"),
                          Column("dept_id", "INTEGER")};
    definition.indexes = {Index("uq_email", {"email"}, true)};
    definition.foreign_keys = {ForeignKey("fk_users_0", {"dept_id"}, "departments", {"id"})};
    return definition;
}

void TestSqliteRoundtrip() {
    auto connection = OpenMemory();
    if (!connection) return;

    Expect(Exec(*connection, "CREATE TABLE departments(id INTEGER PRIMARY KEY, title TEXT)"),
           "创建父表 departments");

    const DdlTableDefinition users = SqliteUsers();
    DdlPlan create;
    std::string error;
    Expect(silkit::db::BuildDdlPlan(DdlMode::Create, silkit::db::Engine::Sqlite, "main", {},
                                    users, "", create, error),
           "生成 SQLite CREATE: " + error);
    const std::string create_sql = JoinPlan(create);
    ExpectContains(create_sql, "\"main\".\"users\"", "SQLite 建表带库名");
    ExpectContains(create_sql, "PRIMARY KEY AUTOINCREMENT", "SQLite 自增主键");
    ExpectContains(create_sql, "CONSTRAINT \"fk_users_0\" FOREIGN KEY", "SQLite 外键");
    ExpectContains(create_sql, "CREATE UNIQUE INDEX \"main\".\"uq_email\"", "SQLite 唯一索引");
    Expect(ExecPlan(*connection, create), "执行 SQLite CREATE 计划");

    auto info = Run(*connection, "PRAGMA main.table_info('users')");
    Expect(info.ok && info.rows.size() == 4, "SQLite 建表后列数为 4");
    bool pk_found = false;
    bool not_null_found = false;
    for (const auto& row : info.rows) {
        if (Text(row, 1) == "id" && Text(row, 5) == "1") pk_found = true;
        if (Text(row, 1) == "name" && Text(row, 3) == "1") not_null_found = true;
    }
    Expect(pk_found, "SQLite id 为主键");
    Expect(not_null_found, "SQLite name 为 NOT NULL");

    auto indexes = Run(*connection, "PRAGMA main.index_list('users')");
    bool unique_index = false;
    for (const auto& row : indexes.rows) {
        if (Text(row, 1) == "uq_email" && Text(row, 2) == "1") unique_index = true;
    }
    Expect(unique_index, "SQLite 唯一索引生效");

    auto fks = Run(*connection, "PRAGMA main.foreign_key_list('users')");
    bool fk_found = false;
    for (const auto& row : fks.rows) {
        if (Text(row, 2) == "departments" && Text(row, 3) == "dept_id") fk_found = true;
    }
    Expect(fk_found, "SQLite 外键生效");

    DdlTableDefinition loaded;
    silkit::db::DbError load_error;
    Expect(silkit::db::LoadDdlDefinition(*connection, "main", "users", loaded, load_error),
           "加载 SQLite 表定义: " + load_error.message);
    Expect(loaded.columns.size() == 4, "加载列数为 4");
    Expect(!loaded.indexes.empty(), "加载索引");
    Expect(loaded.foreign_keys.size() == 1, "加载外键");

    // 改表：删索引、列重命名、加列、加索引
    DdlTableDefinition updated = loaded;
    updated.columns[2].name = "mail";
    updated.columns[2].old_name = "email";
    updated.columns.push_back(Column("age", "INTEGER"));
    updated.columns.back().has_default = true;
    updated.columns.back().default_expr = "0";
    updated.indexes.clear();
    updated.indexes.push_back(Index("idx_age", {"age"}));

    DdlPlan alter;
    error.clear();
    Expect(silkit::db::BuildDdlPlan(DdlMode::Alter, silkit::db::Engine::Sqlite, "main", loaded,
                                    updated, "", alter, error),
           "生成 SQLite ALTER: " + error);
    const std::string alter_sql = JoinPlan(alter);
    ExpectContains(alter_sql, "DROP INDEX \"main\".\"uq_email\"", "SQLite ALTER 删除索引");
    ExpectContains(alter_sql, "RENAME COLUMN \"email\" TO \"mail\"", "SQLite ALTER 重命名列");
    ExpectContains(alter_sql, "ADD COLUMN \"age\" INTEGER DEFAULT 0", "SQLite ALTER 新增列");
    ExpectContains(alter_sql, "CREATE INDEX \"main\".\"idx_age\"", "SQLite ALTER 新增索引");
    Expect(ExecPlan(*connection, alter), "执行 SQLite ALTER 计划");

    info = Run(*connection, "PRAGMA main.table_info('users')");
    bool mail_found = false;
    bool age_found = false;
    for (const auto& row : info.rows) {
        if (Text(row, 1) == "mail") mail_found = true;
        if (Text(row, 1) == "age") age_found = true;
    }
    Expect(mail_found && age_found, "SQLite 列重命名/新增生效");

    // 修改列定义应给出明确错误
    DdlTableDefinition bad = loaded;
    bad.columns[1].type = "BLOB";
    DdlPlan unsupported;
    error.clear();
    Expect(!silkit::db::BuildDdlPlan(DdlMode::Alter, silkit::db::Engine::Sqlite, "main", loaded,
                                     bad, "", unsupported, error),
           "SQLite 修改列定义被拒绝");
    ExpectContains(error, "不支持修改列定义", "SQLite 修改列错误原因");

    // 表重命名
    DdlTableDefinition loaded_again;
    Expect(silkit::db::LoadDdlDefinition(*connection, "main", "users", loaded_again, load_error),
           "重新加载 SQLite 表定义");
    DdlTableDefinition renamed = loaded_again;
    renamed.name = "members";
    renamed.indexes.clear();
    DdlPlan rename_plan;
    error.clear();
    Expect(silkit::db::BuildDdlPlan(DdlMode::Alter, silkit::db::Engine::Sqlite, "main",
                                    loaded_again, renamed, "", rename_plan, error),
           "生成 SQLite 表重命名: " + error);
    ExpectContains(JoinPlan(rename_plan), "RENAME TO \"members\"", "SQLite 表重命名 SQL");
    Expect(ExecPlan(*connection, rename_plan), "执行 SQLite 重命名");
    Expect(Run(*connection, "SELECT COUNT(*) FROM members").ok, "重命名后的表可查询");

    DdlPlan drop;
    error.clear();
    Expect(silkit::db::BuildDdlPlan(DdlMode::Drop, silkit::db::Engine::Sqlite, "main", {},
                                    renamed, "", drop, error),
           "生成 SQLite DROP: " + error);
    ExpectContains(JoinPlan(drop), "DROP TABLE \"main\".\"members\"", "SQLite DROP SQL");
    Expect(ExecPlan(*connection, drop), "执行 SQLite DROP");
    Expect(!Run(*connection, "SELECT COUNT(*) FROM members").ok, "DROP 后表不存在");

    DdlPlan truncate;
    error.clear();
    Expect(silkit::db::BuildDdlPlan(DdlMode::Truncate, silkit::db::Engine::Sqlite, "main", {},
                                    users, "", truncate, error),
           "生成 SQLite TRUNCATE: " + error);
    ExpectContains(JoinPlan(truncate), "DELETE FROM \"main\".\"users\"", "SQLite 清空表 SQL");
}

void TestMysqlSql() {
    DdlTableDefinition definition;
    definition.engine = silkit::db::Engine::MySql;
    definition.scope = "app";
    definition.name = "users";
    definition.columns = {Column("id", "INT", false, true, true),
                          Column("name", "VARCHAR(64)", true),
                          Column("dept_id", "INT")};
    definition.indexes = {Index("uq_name", {"name"}, true)};
    definition.foreign_keys = {
        ForeignKey("fk_dept", {"dept_id"}, "departments", {"id"}, "CASCADE")};
    definition.options.engine = "InnoDB";
    definition.options.charset = "utf8mb4";
    definition.options.collation = "utf8mb4_general_ci";
    definition.options.comment = "用户表";

    DdlPlan create;
    std::string error;
    Expect(silkit::db::BuildDdlPlan(DdlMode::Create, silkit::db::Engine::MySql, "app", {},
                                    definition, "", create, error),
           "生成 MySQL CREATE: " + error);
    const std::string sql = JoinPlan(create);
    ExpectContains(sql, "CREATE TABLE `app`.`users`", "MySQL 建表");
    ExpectContains(sql, "`id` INT AUTO_INCREMENT", "MySQL 自增列");
    ExpectContains(sql, "PRIMARY KEY (`id`)", "MySQL 主键");
    ExpectContains(sql, "UNIQUE KEY `uq_name` (`name`)", "MySQL 唯一索引");
    ExpectContains(sql, "FOREIGN KEY (`dept_id`) REFERENCES `app`.`departments` (`id`)",
                   "MySQL 外键引用");
    ExpectContains(sql, "ON DELETE CASCADE", "MySQL 级联删除");
    ExpectContains(sql, "ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci",
                   "MySQL 表选项");
    ExpectContains(sql, "COMMENT='用户表'", "MySQL 表注释");

    DdlTableDefinition updated = definition;
    updated.name = "members";
    updated.columns[1].type = "VARCHAR(128)";
    updated.columns.push_back(Column("email", "VARCHAR(255)"));
    updated.options.comment = "成员表";

    DdlPlan alter;
    error.clear();
    Expect(silkit::db::BuildDdlPlan(DdlMode::Alter, silkit::db::Engine::MySql, "app", definition,
                                    updated, "", alter, error),
           "生成 MySQL ALTER: " + error);
    const std::string alter_sql = JoinPlan(alter);
    ExpectContains(alter_sql, "RENAME TABLE `app`.`users` TO `app`.`members`", "MySQL 表重命名");
    ExpectContains(alter_sql, "MODIFY COLUMN `name` VARCHAR(128) NOT NULL", "MySQL 修改列");
    ExpectContains(alter_sql, "ADD COLUMN `email` VARCHAR(255)", "MySQL 新增列");
    ExpectContains(alter_sql, "ENGINE=InnoDB", "MySQL 表选项变更");
    ExpectContains(alter_sql, "COMMENT='成员表'", "MySQL 表注释变更");
    ExpectNotContains(alter_sql, "START TRANSACTION", "MySQL ALTER 无事务包裹");

    DdlTableDefinition drop_fk = updated;
    drop_fk.foreign_keys.clear();
    for (auto it = drop_fk.columns.begin(); it != drop_fk.columns.end(); ++it) {
        if (it->name == "dept_id") {
            drop_fk.columns.erase(it);
            break;
        }
    }
    DdlPlan alter_drop;
    error.clear();
    Expect(silkit::db::BuildDdlPlan(DdlMode::Alter, silkit::db::Engine::MySql, "app", updated,
                                    drop_fk, "", alter_drop, error),
           "生成 MySQL 删除外键/列: " + error);
    const std::string drop_sql = JoinPlan(alter_drop);
    ExpectContains(drop_sql, "DROP FOREIGN KEY `fk_dept`", "MySQL 删除外键");
    ExpectContains(drop_sql, "DROP COLUMN `dept_id`", "MySQL 删除列");
    Expect(drop_sql.find("DROP FOREIGN KEY") < drop_sql.find("DROP COLUMN"),
           "MySQL 先删外键再删列");
    Expect(HasWarning(alter_drop, "删除列 dept_id 会丢失该列数据"), "MySQL 删除列警告");
    Expect(alter_drop.affected_rows == "未知", "ALTER 影响行数为未知");

    // 缩短长度与新增唯一索引的预览警告
    DdlTableDefinition narrowed = definition;
    narrowed.columns[1].type = "VARCHAR(32)";
    narrowed.indexes.push_back(Index("uq_mail", {"name"}, true));
    DdlPlan narrow_plan;
    error.clear();
    Expect(silkit::db::BuildDdlPlan(DdlMode::Alter, silkit::db::Engine::MySql, "app", definition,
                                    narrowed, "", narrow_plan, error),
           "生成 MySQL 缩短长度计划: " + error);
    Expect(HasWarning(narrow_plan, "缩短"), "MySQL 缩短长度警告");
    Expect(HasWarning(narrow_plan, "唯一索引 uq_mail"), "MySQL 唯一索引创建警告");

    // 改类型与非空收紧的预览警告
    DdlTableDefinition retyped = definition;
    retyped.columns[0].type = "BIGINT";
    retyped.columns[2].not_null = true;
    DdlPlan retype_plan;
    error.clear();
    Expect(silkit::db::BuildDdlPlan(DdlMode::Alter, silkit::db::Engine::MySql, "app", definition,
                                    retyped, "", retype_plan, error),
           "生成 MySQL 改类型计划: " + error);
    Expect(HasWarning(retype_plan, "类型从 INT 改为 BIGINT"), "MySQL 改类型警告");
    Expect(HasWarning(retype_plan, "改为 NOT NULL"), "MySQL 收紧非空警告");
}

void TestPostgresSql() {
    DdlTableDefinition definition;
    definition.engine = silkit::db::Engine::Postgres;
    definition.scope = "public";
    definition.name = "users";
    definition.primary_key_name = "users_pkey";
    definition.columns = {Column("id", "integer", true, true, true),
                          Column("name", "character varying(64)", true)};
    definition.columns[1].comment = "名称";
    definition.indexes = {Index("uq_name", {"name"}, true)};
    definition.options.comment = "用户表";

    DdlPlan create;
    std::string error;
    Expect(silkit::db::BuildDdlPlan(DdlMode::Create, silkit::db::Engine::Postgres, "public", {},
                                    definition, "", create, error),
           "生成 PostgreSQL CREATE: " + error);
    const std::string sql = JoinPlan(create);
    ExpectContains(sql, "CREATE TABLE \"public\".\"users\"", "PostgreSQL 建表");
    ExpectContains(sql, "GENERATED BY DEFAULT AS IDENTITY", "PostgreSQL 自增标识");
    ExpectContains(sql, "CONSTRAINT \"users_pkey\" PRIMARY KEY (\"id\")",
                   "PostgreSQL 主键约束");
    ExpectContains(sql, "CREATE UNIQUE INDEX \"uq_name\" ON \"public\".\"users\" (\"name\")",
                   "PostgreSQL 唯一索引");
    ExpectContains(sql, "COMMENT ON TABLE \"public\".\"users\" IS '用户表'",
                   "PostgreSQL 表注释");
    ExpectContains(sql, "COMMENT ON COLUMN \"public\".\"users\".\"name\" IS '名称'",
                   "PostgreSQL 列注释");
}

void TestTokenAndValidation() {
    DdlTableDefinition definition;
    definition.engine = silkit::db::Engine::Sqlite;
    definition.scope = "main";
    definition.name = "t";
    definition.columns = {Column("id", "INTEGER")};

    DdlPlan plan;
    std::string error;
    Expect(silkit::db::BuildDdlPlan(DdlMode::Create, silkit::db::Engine::Sqlite, "main", {},
                                    definition, "", plan, error),
           "生成令牌用计划");
    const std::string token = silkit::db::DdlPlanToken(plan);
    Expect(!token.empty(), "令牌非空");
    Expect(token == silkit::db::DdlPlanToken(plan), "相同计划令牌稳定");
    DdlPlan changed = plan;
    changed.statements.push_back("SELECT 1;");
    Expect(token != silkit::db::DdlPlanToken(changed), "计划变化后令牌失效");

    DdlTableDefinition duplicate = definition;
    duplicate.columns.push_back(Column("ID", "INTEGER"));
    DdlPlan invalid;
    error.clear();
    Expect(!silkit::db::BuildDdlPlan(DdlMode::Create, silkit::db::Engine::Sqlite, "main", {},
                                     duplicate, "", invalid, error),
           "重复列被拒绝");
    ExpectContains(error, "列名重复", "重复列错误原因");

    DdlTableDefinition injection = definition;
    injection.columns[0].default_expr = "0; DROP TABLE t";
    injection.columns[0].has_default = true;
    error.clear();
    Expect(!silkit::db::BuildDdlPlan(DdlMode::Create, silkit::db::Engine::Sqlite, "main", {},
                                     injection, "", invalid, error),
           "注入默认值被拒绝");
    ExpectContains(error, "不允许的字符", "注入默认值错误原因");

    DdlTableDefinition fk_bad = definition;
    fk_bad.columns.push_back(Column("pid", "INTEGER"));
    DdlForeignKey fk = ForeignKey("fk_t_p", {"pid"}, "parent", {"id", "extra"});
    fk_bad.foreign_keys.push_back(fk);
    error.clear();
    Expect(!silkit::db::BuildDdlPlan(DdlMode::Create, silkit::db::Engine::Sqlite, "main", {},
                                     fk_bad, "", invalid, error),
           "外键列数量不匹配被拒绝");
    ExpectContains(error, "数量不一致", "外键列数量错误原因");

    // 新对象名严格校验：引号、超长、非法 UTF-8
    DdlTableDefinition quoted = definition;
    quoted.name = "bad\"name";
    error.clear();
    Expect(!silkit::db::BuildDdlPlan(DdlMode::Create, silkit::db::Engine::Sqlite, "main", {},
                                     quoted, "", invalid, error),
           "表名引号被拒绝");
    ExpectContains(error, "引号", "表名引号错误原因");

    DdlTableDefinition long_name = definition;
    long_name.engine = silkit::db::Engine::MySql;
    long_name.name = std::string(65, 'a');
    error.clear();
    Expect(!silkit::db::BuildDdlPlan(DdlMode::Create, silkit::db::Engine::MySql, "main", {},
                                     long_name, "", invalid, error),
           "MySQL 表名超长被拒绝");
    ExpectContains(error, "过长", "MySQL 表名超长错误原因");

    DdlTableDefinition bad_utf8 = definition;
    bad_utf8.name = "bad";
    bad_utf8.name.push_back(static_cast<char>(0xFF));
    error.clear();
    Expect(!silkit::db::BuildDdlPlan(DdlMode::Create, silkit::db::Engine::Sqlite, "main", {},
                                     bad_utf8, "", invalid, error),
           "非法 UTF-8 表名被拒绝");
    ExpectContains(error, "UTF-8", "非法 UTF-8 错误原因");

    Expect(!silkit::db::DdlTransactional(silkit::db::Engine::MySql, DdlMode::Create),
           "MySQL DDL 不事务");    Expect(silkit::db::DdlTransactional(silkit::db::Engine::Sqlite, DdlMode::Alter),
           "SQLite DDL 可事务");
    Expect(!silkit::db::DdlTransactional(silkit::db::Engine::Postgres, DdlMode::DropDatabase),
           "PostgreSQL DROP DATABASE 不事务");
    Expect(!silkit::db::DdlTypeSuggestions(silkit::db::Engine::MySql).empty(),
           "MySQL 类型建议非空");
    Expect(!silkit::db::DdlTypeSuggestions(silkit::db::Engine::Postgres).empty(),
           "PostgreSQL 类型建议非空");
    Expect(!silkit::db::DdlTypeSuggestions(silkit::db::Engine::Sqlite).empty(),
           "SQLite 类型建议非空");
}

}  // namespace

int main() {
    std::printf("ddl_builder tests\n");
    TestSqliteRoundtrip();
    TestMysqlSql();
    TestPostgresSql();
    TestTokenAndValidation();

    if (g_failed == 0) {
        std::printf("  [PASS] %d checks\n", g_total);
        return 0;
    }
    std::printf("  [FAILED] %d/%d checks\n", g_failed, g_total);
    return 1;
}
