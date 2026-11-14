#include "io/dumper.h"

#include "db/ddl_builder.h"
#include "db/engine_dialect.h"
#include "db/identifier.h"
#include "io/exporter.h"

#include <utility>

namespace silkit::io {
namespace {

std::string Qualified(db::Engine engine, const std::string& scope, const std::string& table) {
    if (scope.empty()) return db::QuoteIdentifier(engine, table);
    return db::QuoteIdentifier(engine, scope) + "." + db::QuoteIdentifier(engine, table);
}

bool ListTables(db::Connection& connection, const DumpOptions& options,
                std::vector<std::string>& tables, std::string& error) {
    if (connection.EngineKind() == db::Engine::Sqlite) {
        const std::string prefix = options.scope.empty()
                                       ? std::string{}
                                       : db::QuoteIdentifier(db::Engine::Sqlite, options.scope) + ".";
        const std::string sql = "SELECT name FROM " + prefix +
                                "sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%'"
                                " ORDER BY name";
        db::ExecOptions exec_options;
        exec_options.fetch_size = 1000;
        exec_options.max_cell_bytes = 0;
        std::vector<std::string> names;
        const db::ExecResult result =
            connection.Execute(sql, {}, exec_options, [&names](const db::RowBatch& batch) {
                for (const auto& row : batch.rows) {
                    if (!row.empty() && !row.front().is_null) names.push_back(row.front().text);
                }
                return true;
            });
        if (result.error.code != 0) {
            error = result.error.message;
            return false;
        }
        tables = std::move(names);
        return true;
    }

    const std::string sql = db::ListObjectNamesSql(connection.EngineKind(), options.scope, "tables");
    if (sql.empty()) {
        error = "当前引擎不支持列出表";
        return false;
    }
    db::ExecOptions exec_options;
    exec_options.fetch_size = 1000;
    exec_options.max_cell_bytes = 0;
    std::vector<std::string> names;
    const db::ExecResult result =
        connection.Execute(sql, {}, exec_options, [&names](const db::RowBatch& batch) {
            for (const auto& row : batch.rows) {
                if (!row.empty() && !row.front().is_null) names.push_back(row.front().text);
            }
            return true;
        });
    if (result.error.code != 0) {
        error = result.error.message;
        return false;
    }
    tables = std::move(names);
    return true;
}

}  // namespace

DumpStatus DumpSql(db::Connection& connection, const DumpOptions& options, std::ostream& out,
                   DumpResult& result, std::string& error, const DumpProgress& progress,
                   const std::function<bool()>& cancelled) {
    result = {};
    error.clear();

    std::vector<std::string> tables = options.tables;
    if (tables.empty()) {
        if (!ListTables(connection, options, tables, error)) return DumpStatus::Failed;
    }
    if (tables.empty()) {
        error = "没有可转储的表";
        return DumpStatus::Failed;
    }

    ExportOptions export_options;
    export_options.format = ExportFormat::Sql;
    export_options.engine = options.engine;
    export_options.null_literal = options.null_literal;
    export_options.encoding = options.encoding;

    ExportWriter writer(out, export_options);
    writer.WriteRaw("-- Silkit SQL dump\n-- engine: " + std::string(db::EngineName(options.engine)) +
                    "\n-- tables: " + std::to_string(tables.size()) + "\n\n");

    for (std::size_t i = 0; i < tables.size(); ++i) {
        if (cancelled && cancelled()) {
            writer.Flush();
            return DumpStatus::Cancelled;
        }
        const std::string& table = tables[i];
        const std::string qualified = Qualified(options.engine, options.scope, table);

        if (options.include_structure) {
            db::DdlTableDefinition definition;
            db::DbError load_error;
            if (!db::LoadDdlDefinition(connection, options.scope, table, definition, load_error)) {
                error = "读取表 " + table + " 结构失败: " + load_error.message;
                return DumpStatus::Failed;
            }
            db::DdlPlan plan;
            std::string plan_error;
            if (!db::BuildDdlPlan(db::DdlMode::Create, options.engine, options.scope, {}, definition,
                                  "", plan, plan_error)) {
                error = "生成表 " + table + " DDL 失败: " + plan_error;
                return DumpStatus::Failed;
            }
            if (options.drop_if_exists) {
                writer.WriteRaw("DROP TABLE IF EXISTS " + qualified + ";\n");
            }
            for (const auto& statement : plan.statements) {
                writer.WriteRaw(statement + "\n");
            }
            writer.WriteRaw("\n");
        }

        std::uint64_t table_rows = 0;
        if (options.include_data) {
            db::ExecOptions exec_options;
            exec_options.fetch_size = 1000;
            exec_options.max_rows = 0;
            exec_options.max_cell_bytes = 0;
            bool began = false;
            const db::ExecResult selected =
                connection.Execute("SELECT * FROM " + qualified + ";", {}, exec_options,
                                   [&](const db::RowBatch& batch) {
                                       if (cancelled && cancelled()) return false;
                                       if (!began) {
                                           // 先 Begin 刷掉上一张表的 INSERT，再切换表名。
                                           writer.Begin(batch.columns);
                                           writer.SetTableName(table);
                                           began = true;
                                       }
                                       for (const auto& row : batch.rows) {
                                           writer.WriteRow(row);
                                           ++table_rows;
                                       }
                                       return true;
                                   });
            if (cancelled && cancelled()) {
                writer.Flush();
                return DumpStatus::Cancelled;
            }
            if (selected.error.code != 0) {
                error = "导出表 " + table + " 数据失败: " + selected.error.message;
                return DumpStatus::Failed;
            }
            writer.WriteRaw("\n");
        }

        ++result.tables;
        result.rows += table_rows;
        if (progress) progress(i + 1, tables.size(), table, table_rows);
    }

    writer.Finish();
    if (!writer.ok()) {
        error = "写入转储文件失败";
        return DumpStatus::Failed;
    }
    result.bytes = writer.bytes();
    return DumpStatus::Ok;
}

}  // namespace silkit::io
