// Silkit E2E：真实 MySQL / PostgreSQL 服务器上的端到端用例矩阵（CODEPLAN §12.4）。
//
// 触发条件：SILKIT_MYSQL_HOST / SILKIT_PG_HOST（未设置则整组 SKIP）。
// 写路径：SILKIT_E2E_WRITE=1 才执行 E2E-11（建/删影子表），默认只读。
// 与 integration_db.cpp 的分工：integration_db 负责 M2/M3 冒烟 + M5/M6 写路径，
// 本文件负责错误矩阵、真实数据、类型矩阵、流式、只读拦截、导入导出与执行计划。
//
// 环境变量：
//   SILKIT_MYSQL_HOST/PORT/USER/PASSWORD/DATABASE
//   SILKIT_PG_HOST/PORT/USER/PASSWORD/DATABASE/SCHEMA
//   SILKIT_MYSQL_NOSSL_HOST/PORT/USER/PASSWORD（可选：不声明 CLIENT_SSL 的 MySQL 协议服务端，
//     用于回归 prefer 自动降级 / disable 明文 / require 明确拒绝；未设置则整组 SKIP）
#include "db/driver_registry.h"
#include "db/identifier.h"
#include "db/sql_splitter.h"
#include "db/types.h"
#include "io/csv.h"
#include "io/exporter.h"
#include "io/importer.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace {

using silkit::db::Cell;
using silkit::db::ColumnMeta;
using silkit::db::ConnectParams;
using silkit::db::Connection;
using silkit::db::DbError;
using silkit::db::Engine;
using silkit::db::ExecOptions;
using silkit::db::ExecResult;
using silkit::db::RowBatch;

int g_passed = 0;
int g_failed = 0;
int g_skipped = 0;

std::string Env(const char* key, const std::string& fallback = {}) {
    const char* value = std::getenv(key);
    return value ? value : fallback;
}

/// state: 1=PASS 0=FAIL -1=SKIP
void Report(const char* id, const char* name, const char* engine_label, int state,
            const std::string& detail = {}) {
    const char* mark = state > 0 ? "PASS" : (state == 0 ? "FAIL" : "SKIP");
    std::printf("  [%s] %-8s %-34s %s%s%s\n", mark, id, name, engine_label,
                detail.empty() ? "" : " · ", detail.c_str());
    if (state > 0) {
        ++g_passed;
    } else if (state == 0) {
        ++g_failed;
    } else {
        ++g_skipped;
    }
}

std::string SqlLiteral(const std::string& text) {
    std::string out = "'";
    for (const char ch : text) {
        if (ch == '\'') out += "''";
        out += ch;
    }
    out += "'";
    return out;
}

struct Fetch {
    ExecResult result;
    std::size_t batches = 0;
    std::vector<ColumnMeta> columns;
    std::vector<std::vector<Cell>> sample;
};

Fetch Run(Connection& connection, const std::string& sql, ExecOptions options,
          std::size_t keep = 4) {
    Fetch out;
    out.result = connection.Execute(sql, {}, options, [&](const RowBatch& batch) {
        ++out.batches;
        if (out.columns.empty() && !batch.columns.empty()) out.columns = batch.columns;
        for (const auto& row : batch.rows) {
            if (out.sample.size() < keep) out.sample.push_back(row);
        }
        return true;
    });
    return out;
}

ExecOptions Options(std::size_t fetch_size = 100, std::size_t max_rows = 0) {
    ExecOptions options;
    options.fetch_size = fetch_size;
    options.max_rows = max_rows;
    options.max_cell_bytes = 0;  // 不做单元格截断，便于校验真实值
    return options;
}

std::string Text(const Fetch& fetch, std::size_t row, std::size_t column) {
    if (row >= fetch.sample.size() || column >= fetch.sample[row].size()) return {};
    const Cell& cell = fetch.sample[row][column];
    return cell.is_null ? std::string{} : cell.text;
}

Cell TextCell(const std::string& value) {
    Cell cell;
    cell.kind = silkit::db::ValueKind::Text;
    cell.is_null = false;
    cell.text = value;
    return cell;
}

struct Ctx {
    Engine engine = Engine::Sqlite;
    Connection* connection = nullptr;
    ConnectParams params;
    const char* label = "";
    std::string scope;
    std::vector<std::string> tables;
    bool mysql = false;
    bool write = false;
};

bool HasTable(const Ctx& ctx, const std::string& name) {
    for (const auto& item : ctx.tables) {
        if (item == name) return true;
    }
    return false;
}

std::string Qualified(const Ctx& ctx, const std::string& table) {
    return silkit::db::QuoteIdentifier(ctx.engine, ctx.scope) + "." +
           silkit::db::QuoteIdentifier(ctx.engine, table);
}

// E2E-01 连接握手：版本、库、占位符、能力位
void Case01(Ctx& ctx) {
    DbError error;
    const bool ping = ctx.connection->Ping(error);
    const std::string version = ctx.connection->ServerVersion();
    const std::string database = ctx.connection->CurrentDatabase();
    const auto caps = ctx.connection->Caps();
    Report("E2E-01", "连接握手/版本/能力", ctx.label,
           ping && !version.empty() && !database.empty() ? 1 : 0,
           "v" + version + " db=" + database + " ph=" + ctx.connection->Placeholder(1) +
               " cancel=" + (caps.cancel ? "yes" : "no") +
               " explain=" + (caps.explain ? "yes" : "no"));
}

// E2E-02 错误矩阵：错误密码必须被拒绝（trust 认证环境记 SKIP）
void Case02(Ctx& ctx) {
    ConnectParams bad = ctx.params;
    bad.password = ctx.params.password.empty() ? "silkit_e2e_wrong_pwd"
                                              : ctx.params.password + "_wrong";
    DbError error;
    auto* driver = silkit::db::DriverRegistry::Instance().Find(ctx.engine);
    auto connection = driver ? driver->Open(bad, error) : nullptr;
    if (connection) {
        Report("E2E-02", "错误密码被拒绝", ctx.label, -1, "服务端允许信任认证，无法验证");
        return;
    }
    Report("E2E-02", "错误密码被拒绝", ctx.label, !error.message.empty() ? 1 : 0,
           "code=" + std::to_string(error.code));
}

// E2E-03 错误矩阵：库不存在
void Case03(Ctx& ctx) {
    ConnectParams bad = ctx.params;
    bad.database = "silkit_e2e_no_such_db_9f3a";
    DbError error;
    auto* driver = silkit::db::DriverRegistry::Instance().Find(ctx.engine);
    auto connection = driver ? driver->Open(bad, error) : nullptr;
    Report("E2E-03", "不存在的库被拒绝", ctx.label,
           !connection && !error.message.empty() ? 1 : 0,
           "code=" + std::to_string(error.code));
}

// E2E-04 错误矩阵：语法/对象错误 + 方言感知的语句分割
void Case04(Ctx& ctx) {
    const Fetch bad = Run(*ctx.connection, "SELECT * FROM silkit_e2e_missing_table_9f3a",
                          Options());
    const bool error_ok = bad.result.error.code != 0 && !bad.result.error.message.empty();

    const std::string script = ctx.mysql ? "SELECT 1; -- 注释;\nSELECT ';' AS s; SELECT 3;"
                                         : "SELECT 1; /* ; */ SELECT $$a;b$$ AS s; SELECT 3;";
    const auto statements = silkit::db::SplitSql(
        script, silkit::db::SplitOptionsForEngine(static_cast<int>(ctx.engine)));
    const bool split_ok = statements.size() == 3;

    Report("E2E-04", "错误可读 + 方言语句分割", ctx.label,
           error_ok && split_ok ? 1 : 0,
           "code=" + std::to_string(bad.result.error.code) + " statements=" +
               std::to_string(statements.size()));
}

// E2E-05 对象元数据：真实库的目录可读、列元数据完整
void Case05(Ctx& ctx) {
    const std::string sql = ctx.mysql
                                ? "SELECT table_name FROM information_schema.tables WHERE "
                                  "table_schema = " + SqlLiteral(ctx.scope) +
                                      " ORDER BY table_name"
                                : "SELECT table_name FROM information_schema.tables WHERE "
                                  "table_schema = 'public' ORDER BY table_name";
    const Fetch list = Run(*ctx.connection, sql, Options(100), 200);
    ctx.tables.clear();
    for (const auto& row : list.sample) {
        if (!row.empty() && !row[0].is_null) ctx.tables.push_back(row[0].text);
    }

    bool columns_ok = false;
    std::string detail = "tables=" + std::to_string(list.result.rows);
    if (!ctx.tables.empty()) {
        const std::string sample_sql = "SELECT * FROM " + Qualified(ctx, ctx.tables.front()) +
                                       " LIMIT 1";
        const Fetch probe = Run(*ctx.connection, sample_sql, Options(1));
        columns_ok = probe.result.error.code == 0 && !probe.columns.empty();
        detail += " first=" + ctx.tables.front() + " columns=" +
                  std::to_string(probe.columns.size());
    }
    Report("E2E-05", "对象树/列元数据", ctx.label,
           list.result.error.code == 0 && list.result.rows > 0 && columns_ok ? 1 : 0, detail);
}

// E2E-06 类型矩阵：数值不被误判二进制（守护 charsetnr==63 回归）、DECIMAL/UTF-8/NULL
void Case06(Ctx& ctx) {
    const std::string sql =
        ctx.mysql
            ? "SELECT 9223372036854775807 AS big_int, CAST(12345.6789 AS DECIMAL(20,4)) AS "
              "dec_val, '1985-01-01' AS date_text, 'M' AS enum_text, NULL AS null_val, "
              "'中文🎉' AS utf8_text"
            : "SELECT 9223372036854775807::bigint AS big_int, "
              "12345.6789::numeric(20,4) AS dec_val, DATE '1985-01-01' AS date_val, "
              "'M'::text AS enum_text, NULL::text AS null_val, '中文🎉'::text AS utf8_text";
    const Fetch fetch = Run(*ctx.connection, sql, Options(10));
    bool ok = fetch.result.error.code == 0 && fetch.sample.size() == 1 &&
              fetch.sample[0].size() >= 6;
    if (ok) {
        const auto& row = fetch.sample[0];
        ok = row[0].kind != silkit::db::ValueKind::Binary &&
             Text(fetch, 0, 0) == "9223372036854775807" &&
             Text(fetch, 0, 1).rfind("12345.6789", 0) == 0 && row[4].is_null &&
             Text(fetch, 0, 5) == "中文🎉";
    }
    Report("E2E-06", "类型矩阵/数值非二进制/UTF-8", ctx.label, ok ? 1 : 0,
           "cols=" + std::to_string(fetch.columns.size()));
}

// E2E-07 大结果流式：分批回调、列元数据随首批、行数准确
void Case07(Ctx& ctx) {
    std::string sql = "SELECT g AS n FROM generate_series(1, 5000) AS g";
    if (ctx.mysql) {
        if (ctx.tables.empty()) {
            Report("E2E-07", "5000 行流式分批读取", ctx.label, -1, "无可用的真实表");
            return;
        }
        const std::string table = HasTable(ctx, "salaries")
                                      ? "salaries"
                                      : (HasTable(ctx, "employees") ? "employees"
                                                                    : ctx.tables.front());
        sql = "SELECT * FROM " + Qualified(ctx, table) + " LIMIT 5000";
    }
    const Fetch fetch = Run(*ctx.connection, sql, Options(100), 1);
    const bool ok = fetch.result.error.code == 0 && fetch.result.rows == 5000 &&
                    fetch.batches >= 50 && !fetch.columns.empty();
    Report("E2E-07", "5000 行流式分批读取", ctx.label, ok ? 1 : 0,
           "rows=" + std::to_string(fetch.result.rows) +
               " batches=" + std::to_string(fetch.batches) +
               " elapsed=" + std::to_string(fetch.result.elapsed_ms) + "ms");
}

// E2E-08 行数上限与截断标记
void Case08(Ctx& ctx) {
    std::string sql = "SELECT g AS n FROM generate_series(1, 5000) AS g";
    if (ctx.mysql) {
        if (ctx.tables.empty()) {
            Report("E2E-08", "maxRows 截断标记", ctx.label, -1, "无可用的真实表");
            return;
        }
        const std::string table = HasTable(ctx, "salaries")
                                      ? "salaries"
                                      : (HasTable(ctx, "employees") ? "employees"
                                                                    : ctx.tables.front());
        sql = "SELECT * FROM " + Qualified(ctx, table) + " LIMIT 5000";
    }
    ExecOptions options = Options(10, 37);
    const Fetch fetch = Run(*ctx.connection, sql, options, 1);
    const bool ok = fetch.result.error.code == 0 && fetch.result.rows == 37 &&
                    fetch.result.truncated;
    Report("E2E-08", "maxRows 截断标记", ctx.label, ok ? 1 : 0,
           "rows=" + std::to_string(fetch.result.rows) +
               " truncated=" + (fetch.result.truncated ? "yes" : "no"));
}

// E2E-09 取消入口可用且连接可恢复
void Case09(Ctx& ctx) {
    ctx.connection->Cancel();
    const Fetch fetch = Run(*ctx.connection, "SELECT 1 AS one", Options());
    const bool ok = fetch.result.error.code == 0 && fetch.result.rows == 1 &&
                    Text(fetch, 0, 0) == "1";
    Report("E2E-09", "取消后连接可恢复", ctx.label, ok ? 1 : 0);
}

// E2E-10 只读连接在驱动层拒绝写语句
void Case10(Ctx& ctx) {
    const std::string table = Qualified(ctx, "silkit_e2e_readonly_9f3a");
    ctx.connection->SetReadOnly(true);
    const Fetch blocked = Run(*ctx.connection, "CREATE TABLE " + table + " (id INT)",
                              Options());
    const bool flag = ctx.connection->IsReadOnly();
    ctx.connection->SetReadOnly(false);
    const bool restored = !ctx.connection->IsReadOnly();
    Report("E2E-10", "只读连接拦截写语句", ctx.label,
           blocked.result.error.code != 0 && flag && restored ? 1 : 0,
           "code=" + std::to_string(blocked.result.error.code));
}

// E2E-11 导出 → CSV 解析 → 导入（影子表，默认跳过）
void Case11(Ctx& ctx) {
    if (!ctx.write) {
        Report("E2E-11", "CSV 导出/解析/导入回环", ctx.label, -1,
               "未设置 SILKIT_E2E_WRITE=1");
        return;
    }

    std::vector<ColumnMeta> columns(3);
    columns[0].name = "name";
    columns[0].kind = silkit::db::ValueKind::Text;
    columns[1].name = "note";
    columns[1].kind = silkit::db::ValueKind::Text;
    columns[2].name = "score";
    columns[2].kind = silkit::db::ValueKind::Decimal;

    const std::vector<std::vector<Cell>> rows = {
        {TextCell("林知夏"), TextCell("含,逗号和\"引号\""), TextCell("88.5")},
        {TextCell("中文🎉"), TextCell(""), TextCell("0")},
        {TextCell("Alice"), TextCell("普通"), TextCell("12")},
    };

    std::ostringstream stream;
    silkit::io::ExportOptions export_options;
    export_options.format = silkit::io::ExportFormat::Csv;
    export_options.engine = ctx.engine;
    silkit::io::ExportWriter writer(stream, export_options);
    writer.Begin(columns);
    for (const auto& row : rows) writer.WriteRow(row);
    writer.Finish();
    const std::string csv_text = stream.str();

    silkit::io::CsvParser parser(csv_text);
    std::vector<std::string> fields;
    std::size_t records = 0;
    bool parser_ok = true;
    while (parser.Next(fields)) {
        ++records;
        if (records == 2 && (fields.size() < 2 || fields[1] != "含,逗号和\"引号\"")) {
            parser_ok = false;
        }
    }
    parser_ok = parser_ok && !parser.failed() && records == 4 && writer.rows() == 3 &&
                writer.ok();

    const std::string table = "silkit_e2e_csv_probe";
    const std::string qualified = Qualified(ctx, table);
    Run(*ctx.connection, "DROP TABLE IF EXISTS " + qualified, Options());

    silkit::io::CsvImportOptions import_options;
    import_options.has_header = true;
    import_options.create_table = true;
    import_options.batch_size = 2;
    import_options.scope = ctx.scope;
    import_options.table = table;
    silkit::io::CsvImportResult import_result;
    silkit::io::CsvImportError import_error;
    const auto status = silkit::io::ImportCsv(*ctx.connection, csv_text, {}, import_options,
                                              import_result, import_error);
    const Fetch count = Run(*ctx.connection, "SELECT COUNT(*) FROM " + qualified, Options());
    const Fetch utf8 = Run(*ctx.connection,
                           "SELECT COUNT(*) FROM " + qualified + " WHERE name LIKE '%中文%'",
                           Options());
    const bool import_ok = status == silkit::io::ImportStatus::Ok &&
                           Text(count, 0, 0) == "3" && Text(utf8, 0, 0) == "1";
    Run(*ctx.connection, "DROP TABLE IF EXISTS " + qualified, Options());

    Report("E2E-11", "CSV 导出/解析/导入回环", ctx.label,
           parser_ok && import_ok ? 1 : 0,
           "export_rows=" + std::to_string(writer.rows()) +
               " csv_records=" + std::to_string(records) +
               " import_rows=" + std::to_string(import_result.rows) +
               (import_error.message.empty() ? "" : " err=" + import_error.message));
}

// E2E-12 真实业务数据聚合正确性（employees 示例库 / PG 等价序列）
void Case12(Ctx& ctx) {
    const bool has_departments = HasTable(ctx, "departments");
    const std::string sql = has_departments
                                ? "SELECT COUNT(*) AS n FROM " + Qualified(ctx, "departments")
                                : "SELECT COUNT(*) AS n FROM generate_series(1, 9) AS g";
    const Fetch fetch = Run(*ctx.connection, sql, Options());
    const bool ok = fetch.result.error.code == 0 && Text(fetch, 0, 0) == "9";
    Report("E2E-12", "真实数据聚合正确性", ctx.label, ok ? 1 : 0,
           std::string(has_departments ? "departments" : "generate_series") +
               " n=" + Text(fetch, 0, 0));
}

// E2E-13 语句分割偏移可定位
void Case13(Ctx& ctx) {
    const std::string script = "SELECT 1 AS a;\n\nSELECT 2 AS b;";
    const auto statements = silkit::db::SplitSql(
        script, silkit::db::SplitOptionsForEngine(static_cast<int>(ctx.engine)));
    const bool ok = statements.size() == 2 && statements[1].begin_offset > 0 &&
                    statements[1].begin_offset > statements[0].begin_offset;
    Report("E2E-13", "语句分割与错误定位偏移", ctx.label, ok ? 1 : 0,
           "statements=" + std::to_string(statements.size()));
}

// E2E-14 EXPLAIN 执行计划可用
void Case14(Ctx& ctx) {
    const Fetch fetch = Run(*ctx.connection, "EXPLAIN SELECT 1 AS one", Options());
    const bool ok = fetch.result.error.code == 0 && fetch.result.rows > 0;
    Report("E2E-14", "EXPLAIN 执行计划", ctx.label, ok ? 1 : 0,
           "rows=" + std::to_string(fetch.result.rows));
}

// MariaDB Connector/C 3.4 默认强制校验服务端证书，会让「服务端不声明 CLIENT_SSL」
// 的连接无条件报 2026。以下用例覆盖 ssl_mode 的三种语义（CODEPLAN §12.4 SSL-01~03）。
void RunNoSslServer() {
    const std::string host = Env("SILKIT_MYSQL_NOSSL_HOST");
    if (host.empty()) {
        std::printf("  [SKIP] MySQL(no-TLS)：未设置 SILKIT_MYSQL_NOSSL_HOST，跳过整组\n");
        return;
    }
    auto* driver = silkit::db::DriverRegistry::Instance().Find(Engine::MySql);
    if (!driver) {
        ++g_failed;
        std::printf("  [FAIL] MySQL(no-TLS)：驱动未注册\n");
        return;
    }

    const std::string port = Env("SILKIT_MYSQL_NOSSL_PORT", "3307");
    const std::string user = Env("SILKIT_MYSQL_NOSSL_USER", "root");
    const std::string password = Env("SILKIT_MYSQL_NOSSL_PASSWORD");
    std::printf("\nMySQL(no-TLS) · %s:%s\n", host.c_str(), port.c_str());

    struct Mode {
        const char* id;
        const char* name;
        const char* mode;
        bool expect_open;
    };
    const Mode modes[] = {
        {"SSL-01", "prefer 自动降级为明文", "prefer", true},
        {"SSL-02", "disable 强制明文连接", "disable", true},
        {"SSL-03", "require 拒绝无 TLS 服务端", "require", false},
    };
    for (const Mode& mode : modes) {
        ConnectParams params;
        params.engine = Engine::MySql;
        params.host = host;
        params.port = port;
        params.user = user;
        params.password = password;
        params.ssl_mode = mode.mode;
        params.connect_timeout_ms = 5000;
        DbError error;
        auto connection = driver->Open(params, error);
        bool ok = false;
        std::string detail;
        if (mode.expect_open) {
            if (connection) {
                const ExecResult result = connection->Execute(
                    "SELECT 1 AS one", {}, Options(),
                    [](const RowBatch&) { return true; });
                ok = result.error.code == 0 && result.rows > 0;
                detail = "rows=" + std::to_string(result.rows);
                if (!ok) detail += " · " + result.error.message;
            } else {
                detail = "code=" + std::to_string(error.code) + " · " + error.message;
            }
        } else {
            ok = !connection && error.code == 2026 &&
                 error.hint.find("disable") != std::string::npos;
            detail = "code=" + std::to_string(error.code) + " · " +
                     (error.hint.empty() ? error.message : error.hint);
        }
        Report(mode.id, mode.name, "MySQL(no-TLS)", ok ? 1 : 0, detail);
    }
}

void RunEngine(Engine engine, const char* prefix, const char* default_port,
               const char* default_user, const char* default_database, const char* label) {
    const std::string host_key = std::string(prefix) + "_HOST";
    const std::string host = Env(host_key.c_str());
    if (host.empty()) {
        std::printf("  [SKIP] %s：未设置 %s，跳过整组\n", label, host_key.c_str());
        return;
    }

    ConnectParams params;
    params.engine = engine;
    params.host = host;
    params.port = Env((std::string(prefix) + "_PORT").c_str(), default_port);
    params.user = Env((std::string(prefix) + "_USER").c_str(), default_user);
    params.password = Env((std::string(prefix) + "_PASSWORD").c_str());
    params.database = Env((std::string(prefix) + "_DATABASE").c_str(), default_database);
    params.schema = Env((std::string(prefix) + "_SCHEMA").c_str());
    params.connect_timeout_ms = 5000;

    auto* driver = silkit::db::DriverRegistry::Instance().Find(engine);
    if (!driver) {
        ++g_failed;
        std::printf("  [FAIL] %s：驱动未注册\n", label);
        return;
    }
    DbError error;
    auto connection = driver->Open(params, error);
    if (!connection) {
        ++g_failed;
        std::printf("  [FAIL] %s 连接失败：%s (code=%d)\n", label, error.message.c_str(),
                    error.code);
        return;
    }

    Ctx ctx;
    ctx.engine = engine;
    ctx.connection = connection.get();
    ctx.params = params;
    ctx.label = label;
    ctx.mysql = engine == Engine::MySql;
    ctx.write = Env("SILKIT_E2E_WRITE") == "1";
    ctx.scope = ctx.mysql ? (params.database.empty() ? connection->CurrentDatabase()
                                                     : params.database)
                          : (params.schema.empty() ? std::string("public") : params.schema);

    std::printf("\n%s · %s:%s/%s\n", label, params.host.c_str(), params.port.c_str(),
                params.database.c_str());
    Case01(ctx);
    Case02(ctx);
    Case03(ctx);
    Case04(ctx);
    Case05(ctx);
    Case06(ctx);
    Case07(ctx);
    Case08(ctx);
    Case09(ctx);
    Case10(ctx);
    Case11(ctx);
    Case12(ctx);
    Case13(ctx);
    Case14(ctx);
}

}  // namespace

int main() {
    std::printf("Silkit E2E real-database matrix (CODEPLAN 12.4)\n");
    silkit::db::RegisterBuiltinDrivers();
    RunEngine(Engine::MySql, "SILKIT_MYSQL", "3306", "root", "employees", "MySQL");
    RunEngine(Engine::Postgres, "SILKIT_PG", "5432", "postgres", "silkit_test", "PostgreSQL");
    RunNoSslServer();
    std::printf("\n%d passed, %d failed, %d skipped\n", g_passed, g_failed, g_skipped);
    return g_failed == 0 ? 0 : 1;
}
