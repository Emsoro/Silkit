#include "io/exporter.h"

#include "db/identifier.h"
#include "util/base64.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace silkit::io {
namespace {

constexpr std::size_t kBufferLimit = 64 * 1024;
constexpr std::size_t kSqlBatch = 100;

std::string Lower(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool IsNumericText(const std::string& text) {
    if (text.empty()) return false;
    char* end = nullptr;
    std::strtod(text.c_str(), &end);
    return end != nullptr && *end == '\0';
}

std::string HexOf(const std::string& base64) {
    std::string bytes;
    if (!util::Base64Decode(base64, bytes)) return base64;
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) {
        out.push_back(kHex[c >> 4]);
        out.push_back(kHex[c & 0x0F]);
    }
    return out;
}

std::string JsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char* kHex = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(kHex[c >> 4]);
                    out.push_back(kHex[c & 0x0F]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

std::string HtmlEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

std::string MarkdownEscape(std::string text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        if (c == '|') {
            out += "\\|";
        } else if (c == '\n') {
            out += "<br>";
        } else if (c == '\r') {
            // 丢弃 CR，避免破坏 Markdown 表格。
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string CellText(const db::Cell& cell, const std::string& null_literal) {
    if (cell.is_null) return null_literal;
    return cell.text;
}

/// UTF-8 安全截断：保证不切在续字节上。
std::size_t SafeUtf8Cut(const std::string& text, std::size_t limit) {
    std::size_t cut = std::min(limit, text.size());
    while (cut > 0 && cut < text.size() &&
           (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    return cut;
}

}  // namespace

const char* ExportFormatName(ExportFormat format) {
    switch (format) {
        case ExportFormat::Json: return "json";
        case ExportFormat::Sql: return "sql";
        case ExportFormat::Markdown: return "markdown";
        case ExportFormat::Html: return "html";
        case ExportFormat::Csv:
        default: return "csv";
    }
}

bool ExportFormatFromName(const std::string& name, ExportFormat& out) {
    const std::string key = Lower(name);
    if (key.empty() || key == "csv") {
        out = ExportFormat::Csv;
        return true;
    }
    if (key == "json") {
        out = ExportFormat::Json;
        return true;
    }
    if (key == "sql") {
        out = ExportFormat::Sql;
        return true;
    }
    if (key == "markdown" || key == "md") {
        out = ExportFormat::Markdown;
        return true;
    }
    if (key == "html" || key == "htm") {
        out = ExportFormat::Html;
        return true;
    }
    return false;
}

ExportWriter::ExportWriter(std::ostream& out, ExportOptions options)
    : out_(out), options_(std::move(options)) {
    if (options_.encoding == TextEncoding::Utf8Bom) {
        out_.write("\xEF\xBB\xBF", 3);
        bytes_ += 3;
    } else if (options_.encoding == TextEncoding::Utf16LE) {
        out_.write("\xFF\xFE", 2);
        bytes_ += 2;
    }
}

void ExportWriter::Append(const std::string& utf8) {
    if (!ok_ || utf8.empty()) return;
    buffer_ += utf8;
    if (buffer_.size() >= kBufferLimit) FlushLocked();
}

void ExportWriter::FlushLocked() {
    if (buffer_.empty() || !ok_) return;
    const std::size_t cut = SafeUtf8Cut(buffer_, buffer_.size());
    if (cut == 0) return;

    std::string encoded;
    std::string error;
    if (!EncodeFromUtf8(buffer_.substr(0, cut), options_.encoding, encoded, error)) {
        ok_ = false;
        return;
    }
    if (!encoded.empty()) {
        out_.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
        bytes_ += encoded.size();
        if (!out_) ok_ = false;
    }
    buffer_.erase(0, cut);
}

void ExportWriter::Flush() { FlushLocked(); }

void ExportWriter::Begin(const std::vector<db::ColumnMeta>& columns) {
    // 换表时先把上一张表未写出的 INSERT 刷掉。
    if (options_.format == ExportFormat::Sql) FinishSql();
    columns_ = columns;
    sql_values_.clear();
    sql_rows_ = 0;
    first_row_ = true;
    started_ = true;

    switch (options_.format) {
        case ExportFormat::Csv: {
            if (!options_.include_header) break;
            std::string line;
            std::vector<std::string> names;
            names.reserve(columns.size());
            for (const auto& column : columns) names.push_back(column.name);
            WriteCsvRow(line, names, options_.csv);
            Append(line);
            break;
        }
        case ExportFormat::Json:
            Append("[\n");
            break;
        case ExportFormat::Markdown: {
            if (!options_.include_header) break;
            std::string line = "|";
            std::string sep = "|";
            for (const auto& column : columns) {
                line += " " + MarkdownEscape(column.name) + " |";
                sep += " --- |";
            }
            Append(line + "\n" + sep + "\n");
            break;
        }
        case ExportFormat::Html: {
            Append("<table>\n");
            if (options_.include_header) {
                Append("  <thead><tr>");
                for (const auto& column : columns) {
                    Append("<th>" + HtmlEscape(column.name) + "</th>");
                }
                Append("</tr></thead>\n");
            }
            Append("  <tbody>\n");
            break;
        }
        case ExportFormat::Sql:
            break;
    }
}

void ExportWriter::SetTableName(const std::string& table_name) {
    options_.table_name = table_name;
}

void ExportWriter::WriteRow(const std::vector<db::Cell>& row) {
    ++rows_;
    switch (options_.format) {
        case ExportFormat::Csv: {
            std::string line;
            std::vector<std::string> fields;
            fields.reserve(row.size());
            for (const auto& cell : row) {
                fields.push_back(CellText(cell, options_.null_literal));
            }
            WriteCsvRow(line, fields, options_.csv);
            Append(line);
            break;
        }
        case ExportFormat::Json: {
            std::string out;
            if (!first_row_) out += ",\n";
            first_row_ = false;
            out += "  {";
            for (std::size_t i = 0; i < row.size(); ++i) {
                if (i > 0) out += ", ";
                const std::string name = i < columns_.size() ? columns_[i].name
                                                             : "col" + std::to_string(i + 1);
                out += "\"" + JsonEscape(name) + "\": ";
                const db::Cell& cell = row[i];
                if (cell.is_null) {
                    out += "null";
                } else if (cell.kind == db::ValueKind::Bool) {
                    out += (cell.text == "1" || Lower(cell.text) == "true") ? "true" : "false";
                } else if (!cell.is_null && IsNumericText(cell.text) &&
                           (cell.kind == db::ValueKind::Int || cell.kind == db::ValueKind::UInt ||
                            cell.kind == db::ValueKind::Float ||
                            cell.kind == db::ValueKind::Decimal)) {
                    out += cell.text;
                } else if (cell.kind == db::ValueKind::Binary) {
                    out += "\"" + HexOf(cell.text) + "\"";
                } else {
                    out += "\"" + JsonEscape(cell.text) + "\"";
                }
            }
            out += "}";
            Append(out);
            break;
        }
        case ExportFormat::Markdown: {
            std::string line = "|";
            for (const auto& cell : row) {
                line += " " + MarkdownEscape(CellText(cell, options_.null_literal)) + " |";
            }
            Append(line + "\n");
            break;
        }
        case ExportFormat::Html: {
            Append("    <tr>");
            for (const auto& cell : row) {
                Append("<td>" + HtmlEscape(CellText(cell, options_.null_literal)) + "</td>");
            }
            Append("</tr>\n");
            break;
        }
        case ExportFormat::Sql: {
            std::string tuple = "(";
            for (std::size_t i = 0; i < row.size(); ++i) {
                if (i > 0) tuple += ", ";
                tuple += SqlValue(row[i]);
            }
            tuple += ")";
            if (sql_rows_ > 0) sql_values_ += ", ";
            sql_values_ += tuple;
            ++sql_rows_;
            if (sql_rows_ >= kSqlBatch) FinishSql();
            break;
        }
    }
}

std::string ExportWriter::SqlValue(const db::Cell& cell) const {
    if (cell.is_null) return options_.null_literal.empty() ? "NULL" : options_.null_literal;
    switch (cell.kind) {
        case db::ValueKind::Int:
        case db::ValueKind::UInt:
        case db::ValueKind::Float:
        case db::ValueKind::Decimal:
            return cell.text.empty() ? "NULL" : cell.text;
        case db::ValueKind::Bool: {
            const bool truthy = cell.text == "1" || Lower(cell.text) == "true";
            if (options_.engine == db::Engine::Postgres) return truthy ? "true" : "false";
            return truthy ? "1" : "0";
        }
        case db::ValueKind::Binary: {
            const std::string hex = HexOf(cell.text);
            if (options_.engine == db::Engine::Postgres) {
                return "decode('" + hex + "', 'hex')";
            }
            if (options_.engine == db::Engine::MySql) return "x'" + hex + "'";
            return "X'" + hex + "'";
        }
        default:
            return db::QuoteStringLiteral(cell.text);
    }
}

void ExportWriter::FinishSql() {
    if (sql_rows_ == 0) return;
    std::string sql = "INSERT INTO ";
    const std::string table = options_.table_name.empty() ? "data" : options_.table_name;
    const std::size_t dot = table.find('.');
    if (dot == std::string::npos) {
        sql += db::QuoteIdentifier(options_.engine, table);
    } else {
        sql += db::QuoteIdentifier(options_.engine, table.substr(0, dot)) + "." +
               db::QuoteIdentifier(options_.engine, table.substr(dot + 1));
    }
    sql += " (";
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += db::QuoteIdentifier(options_.engine, columns_[i].name);
    }
    sql += ") VALUES " + sql_values_ + ";\n";
    Append(sql);
    sql_values_.clear();
    sql_rows_ = 0;
}

void ExportWriter::WriteRaw(const std::string& utf8) { Append(utf8); }

void ExportWriter::Finish() {
    if (finished_) return;
    finished_ = true;

    switch (options_.format) {
        case ExportFormat::Json:
            Append("\n]\n");
            break;
        case ExportFormat::Html:
            Append("  </tbody>\n</table>\n");
            break;
        case ExportFormat::Sql:
            FinishSql();
            break;
        default:
            break;
    }
    FlushLocked();
    buffer_.clear();
    out_.flush();
    if (!out_) ok_ = false;
    (void)started_;
}

}  // namespace silkit::io
