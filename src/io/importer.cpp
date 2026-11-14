#include "io/importer.h"

#include "db/engine_dialect.h"
#include "db/identifier.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <utility>

namespace silkit::io {
namespace {

struct ParsedRow {
    std::size_t line = 0;
    std::vector<std::string> fields;
};

std::string Qualified(const db::Engine engine, const std::string& scope,
                      const std::string& table) {
    if (scope.empty()) return db::QuoteIdentifier(engine, table);
    return db::QuoteIdentifier(engine, scope) + "." + db::QuoteIdentifier(engine, table);
}

std::string DefaultType(db::Engine engine) {
    return engine == db::Engine::Postgres ? "text" : "TEXT";
}

std::string Trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' ||
                           text[begin] == '\n')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r' || text[end - 1] == '\n')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

db::ExecResult Exec(db::Connection& connection, const std::string& sql,
                    const std::vector<db::Param>& params = {}) {
    db::ExecOptions options;
    options.fetch_size = 1;
    options.max_rows = 0;
    options.max_cell_bytes = 0;
    return connection.Execute(sql, params, options, nullptr);
}

std::string ErrorMessage(const db::DbError& error) {
    std::string message = error.message;
    if (!error.detail.empty()) message += " · " + error.detail;
    return message;
}

}  // namespace

ImportStatus ImportCsv(db::Connection& connection, const std::string& utf8_text,
                       const CsvOptions& csv_options, const CsvImportOptions& options,
                       CsvImportResult& result, CsvImportError& error,
                       const ImportProgress& progress, const std::function<bool()>& cancelled) {
    const auto started = std::chrono::steady_clock::now();
    result = {};
    error = {};

    CsvParser parser(utf8_text, csv_options);
    std::vector<ParsedRow> rows;
    std::vector<std::string> fields;
    while (parser.Next(fields)) {
        ParsedRow row;
        row.line = parser.line();
        row.fields = fields;
        rows.push_back(std::move(row));
    }
    if (parser.failed()) {
        error.stage = "parse";
        error.message = parser.error();
        error.line = parser.line();
        return ImportStatus::Failed;
    }
    if (rows.empty()) {
        error.stage = "args";
        error.message = "CSV 文件没有数据";
        return ImportStatus::Failed;
    }

    // 列绑定：显式映射优先，否则用表头/自动列名。
    std::vector<CsvImportColumn> columns;
    if (!options.columns.empty()) {
        for (const auto& column : options.columns) {
            if (Trim(column.name).empty()) continue;
            columns.push_back(column);
        }
    } else {
        const std::vector<std::string>& header = rows.front().fields;
        std::set<std::string> used;
        for (std::size_t i = 0; i < header.size(); ++i) {
            std::string name = Trim(options.has_header ? header[i] : std::string{});
            if (name.empty()) name = "column" + std::to_string(i + 1);
            std::string unique = name;
            int suffix = 2;
            while (used.count(unique)) unique = name + "_" + std::to_string(suffix++);
            used.insert(unique);
            columns.push_back(CsvImportColumn{i, unique, std::string{}});
        }
    }
    if (columns.empty()) {
        error.stage = "args";
        error.message = "没有可导入的列";
        return ImportStatus::Failed;
    }

    std::size_t first_data = options.has_header ? 1 : 0;
    if (first_data >= rows.size()) {
        error.stage = "args";
        error.message = "CSV 只有表头，没有数据行";
        return ImportStatus::Failed;
    }
    const std::size_t total = rows.size() - first_data;

    const db::Engine engine = connection.EngineKind();
    const std::string qualified = Qualified(engine, options.scope, options.table);

    // 建表在事务之外：MySQL DDL 会隐式提交。
    if (options.create_table) {
        std::string sql = "CREATE TABLE " + qualified + " (";
        for (std::size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) sql += ", ";
            const std::string type = Trim(columns[i].type).empty() ? DefaultType(engine)
                                                                  : columns[i].type;
            sql += db::QuoteIdentifier(engine, columns[i].name) + " " + type;
        }
        sql += ");";
        const db::ExecResult created = Exec(connection, sql);
        if (created.error.code != 0) {
            error.stage = "create";
            error.message = ErrorMessage(created.error);
            return ImportStatus::Failed;
        }
    }

    const db::ExecResult begun = Exec(connection, "BEGIN;");
    if (begun.error.code != 0) {
        error.stage = "begin";
        error.message = ErrorMessage(begun.error);
        return ImportStatus::Failed;
    }

    const std::size_t batch_size = options.batch_size == 0 ? 500 : options.batch_size;

    auto rollback = [&connection]() {
        Exec(connection, "ROLLBACK;");
    };

    auto make_insert = [&](std::size_t begin, std::size_t end, std::string& sql,
                           std::vector<db::Param>& params) {
        sql = "INSERT INTO " + qualified + " (";
        for (std::size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += db::QuoteIdentifier(engine, columns[i].name);
        }
        sql += ") VALUES ";
        params.clear();
        params.reserve((end - begin) * columns.size());
        std::size_t slot = 1;
        for (std::size_t r = begin; r < end; ++r) {
            if (r > begin) sql += ", ";
            sql += "(";
            const std::vector<std::string>& fields = rows[first_data + r].fields;
            for (std::size_t c = 0; c < columns.size(); ++c) {
                if (c > 0) sql += ", ";
                sql += connection.Placeholder(slot++);
                db::Param param;
                param.kind = db::ValueKind::Text;
                const std::size_t index = columns[c].index;
                const std::string value = index < fields.size() ? fields[index] : std::string{};
                if (options.empty_as_null && value.empty()) {
                    param.is_null = true;
                } else {
                    param.is_null = false;
                    param.text = value;
                }
                params.push_back(std::move(param));
            }
            sql += ")";
        }
        sql += ";";
    };

    // 批量失败时逐行回放定位具体出错行，再整体回滚。
    auto locate_failure = [&](std::size_t begin, std::size_t end, CsvImportError& out) {
        out.record = begin + 1;
        out.line = rows[first_data + begin].line;
        Exec(connection, "BEGIN;");
        for (std::size_t r = begin; r < end; ++r) {
            std::string sql;
            std::vector<db::Param> params;
            make_insert(r, r + 1, sql, params);
            const db::ExecResult row_result = Exec(connection, sql, params);
            if (row_result.error.code != 0) {
                out.record = r + 1;
                out.line = rows[first_data + r].line;
                out.message = ErrorMessage(row_result.error);
                break;
            }
        }
        rollback();
        out.rolled_back = true;
    };

    for (std::size_t begin = 0; begin < total; begin += batch_size) {
        if (cancelled && cancelled()) {
            rollback();
            result.rows = begin;
            return ImportStatus::Cancelled;
        }
        const std::size_t end = std::min(total, begin + batch_size);
        std::string sql;
        std::vector<db::Param> params;
        make_insert(begin, end, sql, params);
        const db::ExecResult inserted = Exec(connection, sql, params);
        if (inserted.error.code != 0) {
            rollback();
            error.stage = "insert";
            if (end - begin == 1) {
                error.message = ErrorMessage(inserted.error);
                error.record = begin + 1;
                error.line = rows[first_data + begin].line;
                error.rolled_back = true;
            } else {
                // 事务已回滚，需从第 1 行重放才能正确定位唯一键等跨批冲突。
                locate_failure(0, end, error);
            }
            if (error.message.empty()) error.message = ErrorMessage(inserted.error);
            result.rows = begin;
            return ImportStatus::Failed;
        }
        result.rows = end;
        ++result.batches;
        if (progress) progress(result.rows, total);
    }

    if (cancelled && cancelled()) {
        rollback();
        return ImportStatus::Cancelled;
    }

    const db::ExecResult committed = Exec(connection, "COMMIT;");
    if (committed.error.code != 0) {
        rollback();
        error.stage = "commit";
        error.message = ErrorMessage(committed.error);
        error.rolled_back = true;
        return ImportStatus::Failed;
    }

    result.elapsed_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
    return ImportStatus::Ok;
}

}  // namespace silkit::io
