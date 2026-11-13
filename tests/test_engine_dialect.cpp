// M2/M3 方言层单元测试：无需真实数据库服务，验证类型映射、错误提示、
// 默认端口与元数据 SQL 生成（含字面量转义）。
#include "db/engine_dialect.h"
#include "db/types.h"

#include <cstdio>
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

bool Contains(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

}  // namespace

int main() {
    using namespace silkit::db;

    std::printf("Silkit M2/M3 engine dialect tests\n");

    // ---------------------------------------------------------------- 默认值
    Expect(std::string(DefaultPort(Engine::MySql)) == "3306", "MySQL 默认端口 3306");
    Expect(std::string(DefaultPort(Engine::Postgres)) == "5432", "PG 默认端口 5432");
    Expect(DefaultDatabase(Engine::Postgres) == "postgres", "PG 默认数据库 postgres");

    // ---------------------------------------------------------------- MySQL 类型
    Expect(KindFromMySqlFieldType(3, 32, "int unsigned") == ValueKind::UInt,
           "MySQL int unsigned → UInt");
    Expect(KindFromMySqlFieldType(3, 0, "int") == ValueKind::Int, "MySQL int → Int");
    Expect(KindFromMySqlFieldType(245, 0, "json") == ValueKind::Json, "MySQL json → Json");
    Expect(KindFromMySqlFieldType(252, 128, "blob") == ValueKind::Binary,
           "MySQL blob + binary flag → Binary");
    Expect(KindFromMySqlFieldType(252, 0, "text") == ValueKind::Text,
           "MySQL text（无 binary flag）→ Text");
    Expect(KindFromMySqlFieldType(12, 0, "datetime") == ValueKind::DateTime,
           "MySQL datetime → DateTime");

    // ---------------------------------------------------------------- PG 类型
    Expect(KindFromPgOid(17) == ValueKind::Binary, "PG bytea → Binary");
    Expect(KindFromPgOid(3802) == ValueKind::Json, "PG jsonb → Json");
    Expect(KindFromPgOid(2950) == ValueKind::Uuid, "PG uuid → Uuid");
    Expect(KindFromPgOid(1184) == ValueKind::Timestamp, "PG timestamptz → Timestamp");
    Expect(KindFromPgOid(1007) == ValueKind::Array, "PG int4[] → Array");
    Expect(KindFromPgOid(1700) == ValueKind::Decimal, "PG numeric → Decimal");
    Expect(KindFromPgOid(25) == ValueKind::Text, "PG text → Text");

    // ---------------------------------------------------------------- 错误提示
    {
        DbError error;
        error.code = 1045;
        DecorateError(Engine::MySql, error);
        Expect(!error.hint.empty(), "MySQL 1045 有友好提示");
        error = DbError{};
        error.code = 2006;
        DecorateError(Engine::MySql, error);
        Expect(Contains(error.hint, "重新连接"), "MySQL 2006 提示重新连接");
        error = DbError{};
        error.sql_state = "42P01";
        DecorateError(Engine::Postgres, error);
        Expect(!error.hint.empty(), "PG 42P01 有友好提示");
        error = DbError{};
        error.sql_state = "28P01";
        DecorateError(Engine::Postgres, error);
        Expect(!error.hint.empty(), "PG 28P01 有友好提示");
        error = DbError{};
        error.sql_state = "57014";
        DecorateError(Engine::Postgres, error);
        Expect(error.hint.empty(), "PG 57014（用户取消）不附加错误提示");
    }

    // ---------------------------------------------------------------- 元数据 SQL
    {
        const std::string mysql_columns = ListColumnsSql(Engine::MySql, "appdb", "users");
        Expect(Contains(mysql_columns, "information_schema.COLUMNS"), "MySQL 列元数据走 information_schema");
        Expect(Contains(mysql_columns, "TABLE_SCHEMA = 'appdb'"), "MySQL 列元数据带库名过滤");

        const std::string pg_columns = ListColumnsSql(Engine::Postgres, "public", "users");
        Expect(Contains(pg_columns, "pg_attribute"), "PG 列元数据走 pg_catalog");
        Expect(Contains(pg_columns, "n.nspname = 'public'"), "PG 列元数据带 schema 过滤");

        const std::string ddl = DdlSql(Engine::MySql, "table", "appdb", "users");
        Expect(Contains(ddl, "SHOW CREATE TABLE"), "MySQL 表 DDL 用 SHOW CREATE TABLE");
        Expect(DdlSql(Engine::Postgres, "table", "public", "users").empty(),
               "PG 表 DDL 由上层目录重建");
        Expect(Contains(DdlSql(Engine::Postgres, "view", "public", "v"), "pg_get_viewdef"),
               "PG 视图 DDL 用 pg_get_viewdef");

        Expect(Contains(SwitchDatabaseSql(Engine::MySql, "appdb"), "USE"), "MySQL 切库用 USE");
        Expect(SwitchDatabaseSql(Engine::Postgres, "other").empty(), "PG 不支持原地切库");

        const std::string search = SearchObjectsSql(Engine::MySql, "appdb", "a'b", 10);
        Expect(Contains(search, "a''b"), "搜索关键字中的单引号被转义");
        Expect(Contains(search, "LIMIT 10"), "搜索结果有上限");

        Expect(Contains(ServerStatsSql(Engine::MySql), "character_set_database"),
               "MySQL stats 查询字符集");
        Expect(Contains(ServerStatsSql(Engine::Postgres), "server_encoding"),
               "PG stats 查询编码");
    }

    std::printf("%d checks, %d failed\n", g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
