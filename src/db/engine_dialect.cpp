#include "db/engine_dialect.h"

#include "db/identifier.h"
#include "db/type_map.h"

#include <algorithm>
#include <cctype>

namespace silkit::db {
namespace {

std::string Lit(const std::string& value) { return QuoteStringLiteral(value); }

std::string MySqlSchemaFilter(const std::string& scope) {
    return "TABLE_SCHEMA = " + Lit(scope);
}

std::string PgSchemaFilter(const std::string& scope) {
    return "n.nspname = " + Lit(scope);
}

std::string MySqlScopeObjects(const std::string& scope, const char* type_condition) {
    return "SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA = " + Lit(scope) +
           " AND " + type_condition + " ORDER BY TABLE_NAME";
}

std::string PgClassObjects(const std::string& scope, const char* relkinds) {
    return "SELECT c.relname FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace WHERE " +
           PgSchemaFilter(scope) + " AND c.relkind IN (" + relkinds + ") ORDER BY c.relname";
}

}  // namespace

const char* DefaultPort(Engine engine) {
    switch (engine) {
    case Engine::MySql: return "3306";
    case Engine::Postgres: return "5432";
    default: return "";
    }
}

std::string DefaultDatabase(Engine engine) {
    switch (engine) {
    case Engine::MySql: return {};
    case Engine::Postgres: return "postgres";
    default: return "main";
    }
}

void DecorateError(Engine engine, DbError& error) {
    if (engine == Engine::MySql) {
        switch (error.code) {
        case 1045:
            error.hint = "用户名或密码错误，或该用户不允许从当前主机登录。";
            break;
        case 1049:
            error.hint = "数据库不存在，请检查连接配置中的数据库名。";
            break;
        case 1064:
            error.hint = "SQL 语法错误；MySQL 与 SQLite/PG 的方言可能不同。";
            break;
        case 1146:
            error.hint = "表或视图不存在，或当前数据库选择有误。";
            break;
        case 1062:
            error.hint = "唯一键冲突，请检查主键/唯一索引的值。";
            break;
        case 1205:
            error.hint = "锁等待超时，稍后重试或检查未提交的事务。";
            break;
        case 1213:
            error.hint = "死锁，事务已被回滚，请重试。";
            break;
        case 2006:
        case 2013:
            error.hint = "与服务器的连接已断开，请使用「重新连接」。";
            break;
        case 2002:
        case 2003:
            error.hint = "无法连接到服务器，请检查主机、端口与防火墙。";
            break;
        case 2026:  // CR_SSL_CONNECTION_ERROR
            error.hint = "TLS/SSL 握手失败，或服务端不支持 SSL（如 MySQL 协议代理）。"
                         "可将「字符集 / SSL」设为 disable；prefer 会自动降级为明文连接。";
            break;
        default:
            break;
        }
        return;
    }
    if (engine != Engine::Postgres) return;

    const std::string state = error.sql_state.size() >= 2 ? error.sql_state.substr(0, 2) : std::string{};
    if (state == "28" || state == "3D") {
        error.hint = "认证失败或数据库不存在，请检查用户、密码与数据库名。";
    } else if (state == "42") {
        error.hint = "SQL 语法/对象名错误；PostgreSQL 标识符请用双引号或小写。";
    } else if (state == "23") {
        error.hint = "约束冲突，请检查主键、外键与非空约束。";
    } else if (state == "53") {
        error.hint = "服务器资源不足（连接数/内存），请稍后重试。";
    } else if (state == "08" || error.sql_state == "57P01") {
        error.hint = "连接被服务器关闭，请使用「重新连接」。";
    } else if (state == "40") {
        error.hint = "事务串行化失败，请重试整个事务。";
    }
}

ValueKind KindFromMySqlFieldType(int field_type, unsigned int flags,
                                  const std::string& decl_type) {
    // enum enum_field_types（mysql_com.h）中的稳定数值，避免 dialect 层依赖客户端头。
    enum {
        kDecimal = 0, kTiny = 1, kShort = 2, kLong = 3, kFloat = 4, kDouble = 5,
        kNull = 6, kTimestamp = 7, kLongLong = 8, kInt24 = 9, kDate = 10,
        kTime = 11, kDateTime = 12, kYear = 13, kNewDate = 14, kVarchar = 15,
        kBit = 16, kJson = 245, kNewDecimal = 246, kEnum = 247, kSet = 248,
        kTinyBlob = 249, kMediumBlob = 250, kLongBlob = 251, kBlob = 252,
        kVarString = 253, kString = 254, kGeometry = 255
    };
    constexpr unsigned int kUnsignedFlag = 32;
    constexpr unsigned int kBinaryFlag = 128;

    switch (field_type) {
    case kTiny:
    case kShort:
    case kLong:
    case kInt24:
    case kLongLong:
    case kYear:
    case kBit:
        return (flags & kUnsignedFlag) != 0 ? ValueKind::UInt : ValueKind::Int;
    case kFloat:
    case kDouble:
        return ValueKind::Float;
    case kDecimal:
    case kNewDecimal:
        return ValueKind::Decimal;
    case kTimestamp:
        return ValueKind::Timestamp;
    case kDateTime:
        return ValueKind::DateTime;
    case kDate:
    case kNewDate:
        return ValueKind::Date;
    case kTime:
        return ValueKind::Time;
    case kJson:
        return ValueKind::Json;
    case kNull:
        return ValueKind::Null;
    case kTinyBlob:
    case kMediumBlob:
    case kLongBlob:
    case kBlob:
    case kGeometry:
        if ((flags & kBinaryFlag) == 0 && KindFromDeclType(decl_type) == ValueKind::Text) {
            return ValueKind::Text;  // TEXT 被报告为 blob 但无 binary flag
        }
        return ValueKind::Binary;
    case kEnum:
    case kSet:
        return ValueKind::Text;
    default:
        if ((flags & kBinaryFlag) != 0) return ValueKind::Binary;
        return KindFromDeclType(decl_type);
    }
}

ValueKind KindFromPgOid(unsigned int oid) {
    // pg_type 中稳定的内置 OID（PG 13–16 一致）。
    switch (oid) {
    case 16: return ValueKind::Bool;
    case 17: return ValueKind::Binary;                       // bytea
    case 20: case 21: case 23: case 26: case 24: case 2202:
    case 2203: case 2204: case 2205: case 2206: case 3734: case 3769:
        return ValueKind::Int;
    case 700: case 701: return ValueKind::Float;
    case 1700: return ValueKind::Decimal;                   // numeric
    case 1082: return ValueKind::Date;
    case 1083: case 1266: return ValueKind::Time;
    case 1114: return ValueKind::DateTime;
    case 1184: return ValueKind::Timestamp;
    case 1186: return ValueKind::Text;                      // interval 保留服务端文本
    case 114: case 3802: return ValueKind::Json;            // json / jsonb
    case 2950: return ValueKind::Uuid;
    // 常见数组类型 OID
    case 1000: case 1001: case 1005: case 1007: case 1009: case 1014: case 1015:
    case 1016: case 1021: case 1022: case 1028: case 1182: case 1183: case 1185:
    case 1187: case 1231: case 199: case 3807: case 2951: case 791:
        return ValueKind::Array;
    default:
        return ValueKind::Text;
    }
}

std::string ListDatabasesSql(Engine engine) {
    if (engine == Engine::MySql) {
        return "SELECT SCHEMA_NAME FROM information_schema.SCHEMATA"
               " WHERE SCHEMA_NAME NOT IN ('information_schema','mysql','performance_schema','sys')"
               " ORDER BY SCHEMA_NAME";
    }
    return "SELECT datname FROM pg_database WHERE datistemplate = false ORDER BY datname";
}

std::string ListSchemasSql() {
    return "SELECT nspname FROM pg_namespace"
           " WHERE nspname NOT LIKE 'pg_%' AND nspname <> 'information_schema'"
           " ORDER BY nspname";
}

std::string ListObjectNamesSql(Engine engine, const std::string& scope,
                               const std::string& folder) {
    if (engine == Engine::MySql) {
        if (folder == "tables") return MySqlScopeObjects(scope, "TABLE_TYPE = 'BASE TABLE'");
        if (folder == "views") return MySqlScopeObjects(scope, "TABLE_TYPE IN ('VIEW','SYSTEM VIEW')");
        if (folder == "routines") {
            return "SELECT ROUTINE_NAME FROM information_schema.ROUTINES WHERE ROUTINE_SCHEMA = " +
                   Lit(scope) + " ORDER BY ROUTINE_NAME";
        }
        if (folder == "events") {
            return "SELECT EVENT_NAME FROM information_schema.EVENTS WHERE EVENT_SCHEMA = " +
                   Lit(scope) + " ORDER BY EVENT_NAME";
        }
        if (folder == "triggers") {
            return "SELECT TRIGGER_NAME, EVENT_OBJECT_TABLE FROM information_schema.TRIGGERS"
                   " WHERE TRIGGER_SCHEMA = " + Lit(scope) + " ORDER BY TRIGGER_NAME";
        }
        if (folder == "indexes") {
            return "SELECT DISTINCT INDEX_NAME, TABLE_NAME FROM information_schema.STATISTICS"
                   " WHERE TABLE_SCHEMA = " + Lit(scope) + " ORDER BY INDEX_NAME";
        }
        return {};
    }

    if (folder == "tables") return PgClassObjects(scope, "'r','p','f'");
    if (folder == "views") return PgClassObjects(scope, "'v','m'");
    if (folder == "sequences") return PgClassObjects(scope, "'S'");
    if (folder == "routines") {
        return "SELECT p.proname FROM pg_proc p JOIN pg_namespace n ON n.oid = p.pronamespace"
               " WHERE " + PgSchemaFilter(scope) + " ORDER BY p.proname";
    }
    if (folder == "triggers") {
        return "SELECT t.tgname, c.relname FROM pg_trigger t"
               " JOIN pg_class c ON c.oid = t.tgrelid"
               " JOIN pg_namespace n ON n.oid = c.relnamespace"
               " WHERE " + PgSchemaFilter(scope) + " AND NOT t.tgisinternal ORDER BY t.tgname";
    }
    if (folder == "indexes") {
        return "SELECT i.relname, c.relname FROM pg_index ix"
               " JOIN pg_class i ON i.oid = ix.indexrelid"
               " JOIN pg_class c ON c.oid = ix.indrelid"
               " JOIN pg_namespace n ON n.oid = i.relnamespace"
               " WHERE " + PgSchemaFilter(scope) + " ORDER BY i.relname";
    }
    return {};
}

std::string ListColumnsSql(Engine engine, const std::string& scope,
                           const std::string& table) {
    if (engine == Engine::MySql) {
        return "SELECT COLUMN_NAME, COLUMN_TYPE, IS_NULLABLE, COLUMN_DEFAULT, EXTRA,"
               " COLUMN_KEY, COLUMN_COMMENT, ORDINAL_POSITION"
               " FROM information_schema.COLUMNS WHERE " + MySqlSchemaFilter(scope) +
               " AND TABLE_NAME = " + Lit(table) + " ORDER BY ORDINAL_POSITION";
    }
    return "SELECT a.attname, format_type(a.atttypid, a.atttypmod), a.attnotnull,"
           " pg_get_expr(ad.adbin, ad.adrelid), a.attidentity <> '', a.attgenerated <> '',"
           " col_description(a.attrelid, a.attnum), a.attnum"
           " FROM pg_attribute a"
           " JOIN pg_class c ON c.oid = a.attrelid"
           " JOIN pg_namespace n ON n.oid = c.relnamespace"
           " LEFT JOIN pg_attrdef ad ON ad.adrelid = a.attrelid AND ad.adnum = a.attnum"
           " WHERE " + PgSchemaFilter(scope) + " AND c.relname = " + Lit(table) +
           " AND a.attnum > 0 AND NOT a.attisdropped ORDER BY a.attnum";
}

std::string ListIndexesSql(Engine engine, const std::string& scope,
                           const std::string& table) {
    if (engine == Engine::MySql) {
        return "SELECT INDEX_NAME, COLUMN_NAME, NON_UNIQUE, SEQ_IN_INDEX"
               " FROM information_schema.STATISTICS WHERE " + MySqlSchemaFilter(scope) +
               " AND TABLE_NAME = " + Lit(table) + " ORDER BY INDEX_NAME, SEQ_IN_INDEX";
    }
    return "SELECT i.relname, pg_get_indexdef(i.oid), ix.indisunique"
           " FROM pg_index ix JOIN pg_class i ON i.oid = ix.indexrelid"
           " JOIN pg_class c ON c.oid = ix.indrelid"
           " JOIN pg_namespace n ON n.oid = c.relnamespace"
           " WHERE " + PgSchemaFilter(scope) + " AND c.relname = " + Lit(table) +
           " ORDER BY i.relname";
}

std::string ListForeignKeysSql(Engine engine, const std::string& scope,
                               const std::string& table) {
    if (engine == Engine::MySql) {
        // MySQL 8 的 KEY_COLUMN_USAGE 没有 UPDATE_RULE/DELETE_RULE，需要关联
        // REFERENTIAL_CONSTRAINTS 才能拿到级联规则。
        return "SELECT k.CONSTRAINT_NAME, k.COLUMN_NAME, k.REFERENCED_TABLE_NAME,"
               " k.REFERENCED_COLUMN_NAME, r.UPDATE_RULE, r.DELETE_RULE"
               " FROM information_schema.KEY_COLUMN_USAGE k"
               " JOIN information_schema.REFERENTIAL_CONSTRAINTS r"
               " ON r.CONSTRAINT_SCHEMA = k.CONSTRAINT_SCHEMA"
               " AND r.CONSTRAINT_NAME = k.CONSTRAINT_NAME AND r.TABLE_NAME = k.TABLE_NAME"
               " WHERE k.TABLE_SCHEMA = " + Lit(scope) + " AND k.TABLE_NAME = " + Lit(table) +
               " AND k.REFERENCED_TABLE_NAME IS NOT NULL"
               " ORDER BY k.CONSTRAINT_NAME, k.ORDINAL_POSITION";
    }
    return "SELECT con.conname, pg_get_constraintdef(con.oid)"
           " FROM pg_constraint con JOIN pg_class c ON c.oid = con.conrelid"
           " JOIN pg_namespace n ON n.oid = c.relnamespace"
           " WHERE " + PgSchemaFilter(scope) + " AND c.relname = " + Lit(table) +
           " AND con.contype = 'f' ORDER BY con.conname";
}

std::string ListTriggersSql(Engine engine, const std::string& scope,
                           const std::string& table) {
    if (engine == Engine::MySql) {
        return "SELECT TRIGGER_NAME, ACTION_STATEMENT FROM information_schema.TRIGGERS"
               " WHERE TRIGGER_SCHEMA = " + Lit(scope) + " AND EVENT_OBJECT_TABLE = " + Lit(table) +
               " ORDER BY TRIGGER_NAME";
    }
    return "SELECT t.tgname, pg_get_triggerdef(t.oid) FROM pg_trigger t"
           " JOIN pg_class c ON c.oid = t.tgrelid"
           " JOIN pg_namespace n ON n.oid = c.relnamespace"
           " WHERE " + PgSchemaFilter(scope) + " AND c.relname = " + Lit(table) +
           " AND NOT t.tgisinternal ORDER BY t.tgname";
}

std::string ListPrimaryKeysSql(Engine engine, const std::string& scope,
                               const std::string& table) {
    if (engine == Engine::MySql) {
        return "SELECT COLUMN_NAME FROM information_schema.COLUMNS WHERE " +
               MySqlSchemaFilter(scope) + " AND TABLE_NAME = " + Lit(table) +
               " AND COLUMN_KEY = 'PRI' ORDER BY ORDINAL_POSITION";
    }
    return "SELECT a.attname FROM pg_index ix"
           " JOIN pg_class c ON c.oid = ix.indrelid"
           " JOIN pg_namespace n ON n.oid = c.relnamespace"
           " JOIN pg_attribute a ON a.attrelid = ix.indrelid AND a.attnum = ANY(ix.indkey)"
           " WHERE " + PgSchemaFilter(scope) + " AND c.relname = " + Lit(table) +
           " AND ix.indisprimary ORDER BY a.attnum";
}

std::string ListUniqueIndexColumnsSql(Engine engine, const std::string& scope,
                                      const std::string& table) {
    if (engine == Engine::MySql) {
        return "SELECT INDEX_NAME, COLUMN_NAME FROM information_schema.STATISTICS"
               " WHERE " + MySqlSchemaFilter(scope) + " AND TABLE_NAME = " + Lit(table) +
               " AND NON_UNIQUE = 0 AND SUB_PART IS NULL"
               " ORDER BY INDEX_NAME, SEQ_IN_INDEX";
    }
    if (engine == Engine::Postgres) {
        return "SELECT i.relname, a.attname FROM pg_index ix"
               " JOIN pg_class i ON i.oid = ix.indexrelid"
               " JOIN pg_class c ON c.oid = ix.indrelid"
               " JOIN pg_namespace n ON n.oid = c.relnamespace"
               " JOIN pg_attribute a ON a.attrelid = ix.indrelid AND a.attnum = ANY(ix.indkey)"
               " WHERE " + PgSchemaFilter(scope) + " AND c.relname = " + Lit(table) +
               " AND ix.indisunique AND ix.indpred IS NULL AND ix.indexprs IS NULL"
               " ORDER BY i.relname, a.attnum";
    }
    return {};
}

std::string DdlSql(Engine engine, const std::string& kind,
                    const std::string& scope, const std::string& name) {
    const std::string quoted = QuoteIdentifier(engine, scope) + "." + QuoteIdentifier(engine, name);
    if (engine == Engine::MySql) {
        if (kind == "table") return "SHOW CREATE TABLE " + quoted;
        if (kind == "view") return "SHOW CREATE VIEW " + quoted;
        if (kind == "trigger") {
            return "SELECT ACTION_STATEMENT FROM information_schema.TRIGGERS"
                   " WHERE TRIGGER_SCHEMA = " + Lit(scope) + " AND TRIGGER_NAME = " + Lit(name);
        }
        if (kind == "event") {
            return "SELECT EVENT_DEFINITION FROM information_schema.EVENTS"
                   " WHERE EVENT_SCHEMA = " + Lit(scope) + " AND EVENT_NAME = " + Lit(name);
        }
        if (kind == "routine" || kind == "function" || kind == "procedure") {
            return "SELECT ROUTINE_DEFINITION FROM information_schema.ROUTINES"
                   " WHERE ROUTINE_SCHEMA = " + Lit(scope) + " AND ROUTINE_NAME = " + Lit(name);
        }
        return {};
    }

    const std::string regclass = Lit(scope + "." + name) + "::regclass";
    if (kind == "view" || kind == "materialized_view") {
        return "SELECT pg_get_viewdef(" + regclass + ", true)";
    }
    if (kind == "index") {
        return "SELECT pg_get_indexdef(i.oid) FROM pg_class i"
               " JOIN pg_namespace n ON n.oid = i.relnamespace"
               " WHERE " + PgSchemaFilter(scope) + " AND i.relname = " + Lit(name);
    }
    if (kind == "trigger") {
        return "SELECT pg_get_triggerdef(t.oid) FROM pg_trigger t"
               " JOIN pg_class c ON c.oid = t.tgrelid"
               " JOIN pg_namespace n ON n.oid = c.relnamespace"
               " WHERE " + PgSchemaFilter(scope) + " AND t.tgname = " + Lit(name) +
               " AND NOT t.tgisinternal";
    }
    if (kind == "routine" || kind == "function" || kind == "procedure") {
        return "SELECT pg_get_functiondef(p.oid) FROM pg_proc p"
               " JOIN pg_namespace n ON n.oid = p.pronamespace"
               " WHERE " + PgSchemaFilter(scope) + " AND p.proname = " + Lit(name) + " LIMIT 1";
    }
    if (kind == "sequence") {
        return "SELECT format('CREATE SEQUENCE %I.%I INCREMENT BY %s MINVALUE %s MAXVALUE %s"
               " START WITH %s CACHE %s%s;', n.nspname, c.relname, s.seqincrement, s.seqmin,"
               " s.seqmax, s.seqstart, s.seqcache, CASE WHEN s.seqcycle THEN ' CYCLE' ELSE '' END)"
               " FROM pg_sequence s"
               " JOIN pg_class c ON c.oid = s.seqrelid"
               " JOIN pg_namespace n ON n.oid = c.relnamespace"
               " WHERE " + PgSchemaFilter(scope) + " AND c.relname = " + Lit(name);
    }
    // PG 没有 SHOW CREATE TABLE：由上层从 pg_catalog 重建（M3-5 简化实现）。
    return {};
}

std::string SearchObjectsSql(Engine engine, const std::string& scope,
                             const std::string& keyword, int limit) {
    const std::string term = Lit(keyword);
    const std::string cap = std::to_string(std::clamp(limit, 1, 1000));
    if (engine == Engine::MySql) {
        return "SELECT TABLE_NAME, IF(TABLE_TYPE = 'VIEW', 'view', 'table')"
               " FROM information_schema.TABLES WHERE TABLE_SCHEMA = " + Lit(scope) +
               " AND instr(lower(TABLE_NAME), lower(" + term + ")) > 0"
               " ORDER BY TABLE_NAME LIMIT " + cap;
    }
    return "SELECT c.relname, CASE WHEN c.relkind IN ('v','m') THEN 'view' ELSE 'table' END"
           " FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace"
           " WHERE " + PgSchemaFilter(scope) + " AND c.relkind IN ('r','p','f','v','m')"
           " AND strpos(lower(c.relname), lower(" + term + ")) > 0"
           " ORDER BY c.relname LIMIT " + cap;
}

std::string SwitchDatabaseSql(Engine engine, const std::string& database) {
    if (engine == Engine::MySql) {
        return "USE " + QuoteIdentifier(Engine::MySql, database);
    }
    return {};  // PG 需要新建连接，由 SessionManager 层处理
}

std::string ServerStatsSql(Engine engine) {
    if (engine == Engine::MySql) {
        return "SELECT @@character_set_database, @@time_zone, VERSION()";
    }
    return "SELECT current_setting('server_encoding'), current_setting('TimeZone'),"
           " current_setting('server_version')";
}

std::string ExplainSql(Engine engine, const std::string& sql, bool analyze) {
    const std::string trimmed = sql;
    if (engine == Engine::MySql) {
        return analyze ? "EXPLAIN ANALYZE " + trimmed : "EXPLAIN " + trimmed;
    }
    if (engine == Engine::Postgres) {
        return analyze ? "EXPLAIN (ANALYZE) " + trimmed : "EXPLAIN " + trimmed;
    }
    return "EXPLAIN QUERY PLAN " + trimmed;
}

}  // namespace silkit::db
