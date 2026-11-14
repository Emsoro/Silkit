#pragma once

#include "db/types.h"
#include "io/csv.h"
#include "io/encoding.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace silkit::io {

/// M7-2：CSV / JSON / SQL / Markdown / HTML 流式导出。
enum class ExportFormat { Csv, Json, Sql, Markdown, Html };

const char* ExportFormatName(ExportFormat format);
bool ExportFormatFromName(const std::string& name, ExportFormat& out);

struct ExportOptions {
    ExportFormat format = ExportFormat::Csv;
    CsvOptions csv;
    bool include_header = true;
    std::string null_literal;
    std::string table_name;  // SQL INSERT 使用
    db::Engine engine = db::Engine::Sqlite;
    TextEncoding encoding = TextEncoding::Utf8;
};

/// 写入器内部按 64KB 缓冲，并按编码边界切块转换后写出。
/// CSV/JSON/Markdown 不缓存行；SQL 每 100 行合成一条多值 INSERT。
class ExportWriter {
public:
    ExportWriter(std::ostream& out, ExportOptions options);

    void Begin(const std::vector<db::ColumnMeta>& columns);
    void SetTableName(const std::string& table_name);
    void WriteRow(const std::vector<db::Cell>& row);

    /// 原样写入 UTF-8 文本（转储的 DDL/注释），同样经过编码转换。
    void WriteRaw(const std::string& utf8);

    void Flush();
    void Finish();

    bool ok() const { return ok_; }
    std::uint64_t rows() const { return rows_; }
    std::uint64_t bytes() const { return bytes_; }

private:
    void Append(const std::string& utf8);
    void AppendChar(char c) { Append(std::string(1, c)); }
    void FlushLocked();
    void FinishSql();
    std::string SqlValue(const db::Cell& cell) const;

    std::ostream& out_;
    ExportOptions options_;
    std::vector<db::ColumnMeta> columns_;
    std::string buffer_;
    std::string sql_values_;
    std::size_t sql_rows_ = 0;
    bool started_ = false;
    bool first_row_ = true;
    bool finished_ = false;
    bool ok_ = true;
    std::uint64_t rows_ = 0;
    std::uint64_t bytes_ = 0;
};

}  // namespace silkit::io
