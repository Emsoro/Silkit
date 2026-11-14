// M7 导入/导出基础设施测试：CSV、编码、导出格式、批量导入、SQL 转储恢复。
#include "db/drivers/sqlite_driver.h"
#include "db/sql_splitter.h"
#include "db/types.h"
#include "io/csv.h"
#include "io/dumper.h"
#include "io/encoding.h"
#include "io/exporter.h"
#include "io/importer.h"
#include "util/base64.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace silkit;

int g_failed = 0;
int g_total = 0;

void Expect(bool condition, const std::string& name) {
    ++g_total;
    if (condition) return;
    ++g_failed;
    std::printf("  [FAIL] %s\n", name.c_str());
}

std::unique_ptr<db::Connection> OpenMemory(std::string& error) {
    db::SqliteDriver driver;
    db::ConnectParams params;
    params.file = ":memory:";
    db::DbError open_error;
    auto connection = driver.Open(params, open_error);
    if (!connection) error = open_error.message;
    return connection;
}

db::ExecResult Exec(db::Connection& connection, const std::string& sql,
                    const std::vector<db::Param>& params = {}) {
    db::ExecOptions options;
    options.fetch_size = 100;
    options.max_cell_bytes = 0;
    return connection.Execute(sql, params, options, nullptr);
}

std::uint64_t CountRows(db::Connection& connection, const std::string& table) {
    std::uint64_t count = 0;
    db::ExecOptions options;
    options.fetch_size = 10;
    options.max_cell_bytes = 0;
    connection.Execute("SELECT COUNT(*) FROM " + table, {}, options, [&count](const db::RowBatch& batch) {
        if (!batch.rows.empty() && !batch.rows.front().empty()) {
            count = std::stoull(batch.rows.front().front().text);
        }
        return true;
    });
    return count;
}

db::Cell TextCell(const std::string& text) {
    db::Cell cell;
    cell.kind = db::ValueKind::Text;
    cell.is_null = false;
    cell.text = text;
    return cell;
}

db::Cell IntCell(const std::string& text) {
    db::Cell cell;
    cell.kind = db::ValueKind::Int;
    cell.is_null = false;
    cell.text = text;
    return cell;
}

db::Cell NullCell() {
    db::Cell cell;
    cell.kind = db::ValueKind::Null;
    cell.is_null = true;
    return cell;
}

void TestCsv() {
    const std::string text =
        "name,note\r\n"
        "\"Alice\",\"line1\nline2\"\r\n"
        "\"Bob, Jr.\",\"say \"\"hi\"\"\"\r\n";
    io::CsvParser parser(text);
    std::vector<std::string> fields;

    Expect(parser.Next(fields) && fields.size() == 2 && fields[0] == "name" && fields[1] == "note",
           "CSV：表头");
    Expect(parser.line() == 1, "CSV：首条记录行号为 1");

    Expect(parser.Next(fields) && fields.size() == 2 && fields[0] == "Alice" &&
               fields[1] == "line1\nline2",
           "CSV：引号内换行");
    Expect(parser.line() == 2, "CSV：跨行记录的起始行号");

    Expect(parser.Next(fields) && fields[1] == "say \"hi\"", "CSV：引号转义");
    Expect(parser.line() == 4, "CSV：跨行后行号递增");
    Expect(!parser.Next(fields), "CSV：读取到结尾");
    Expect(!parser.failed(), "CSV：正常结束不报错");

    io::CsvOptions options;
    options.delimiter = ';';
    std::string line;
    io::WriteCsvRow(line, {"a;b", "c\"d", "e\nf"}, options);
    io::CsvParser roundtrip(line, options);
    Expect(roundtrip.Next(fields) && fields.size() == 3 && fields[0] == "a;b" &&
               fields[1] == "c\"d" && fields[2] == "e\nf",
           "CSV：写出后可原样解析");

    Expect(io::DetectDelimiter("a;b;c\n1;2;3\n") == ';', "CSV：探测分号");
    Expect(io::DetectDelimiter("a\tb\tc\n1\t2\t3\n") == '\t', "CSV：探测制表符");
    Expect(io::DetectDelimiter("a,b,c\n1,2,3\n") == ',', "CSV：探测逗号");

    io::CsvParser broken("\"unterminated,a\n");
    Expect(!broken.Next(fields) && broken.failed(), "CSV：未闭合引号报错");
}

void TestEncoding() {
    const std::string chinese = "中文测试：Silkit 数据库";
    std::string gbk;
    std::string error;
    Expect(io::EncodeFromUtf8(chinese, io::TextEncoding::Gbk, gbk, error), "编码：UTF-8 → GBK");
    std::string back;
    Expect(io::DecodeToUtf8(gbk, io::TextEncoding::Gbk, back, error) && back == chinese,
           "编码：GBK → UTF-8 往返一致");
    Expect(io::DetectEncoding(gbk) == io::TextEncoding::Gbk, "编码：GBK 自动探测");

    const std::string emoji = "表情 😀 与中文";
    std::string utf16;
    Expect(io::EncodeFromUtf8(emoji, io::TextEncoding::Utf16LE, utf16, error),
           "编码：UTF-8 → UTF-16LE");
    Expect(io::DetectEncoding(utf16) == io::TextEncoding::Utf16LE, "编码：UTF-16LE BOM 探测");
    std::string utf16_back;
    Expect(io::DecodeToUtf8(utf16, io::TextEncoding::Utf16LE, utf16_back, error) &&
               utf16_back == emoji,
           "编码：UTF-16LE 往返保留 emoji");

    std::string bom;
    Expect(io::EncodeFromUtf8(chinese, io::TextEncoding::Utf8Bom, bom, error), "编码：写入 UTF-8 BOM");
    Expect(io::DetectEncoding(bom) == io::TextEncoding::Utf8Bom, "编码：UTF-8 BOM 探测");
    std::string bom_back;
    Expect(io::DecodeToUtf8(bom, io::TextEncoding::Utf8Bom, bom_back, error) && bom_back == chinese,
           "编码：UTF-8 BOM 解码去 BOM");
}

void TestExporter() {
    std::vector<db::ColumnMeta> columns(4);
    columns[0].name = "id";
    columns[1].name = "name";
    columns[2].name = "note";
    columns[3].name = "data";

    std::string binary_raw;
    binary_raw.push_back(static_cast<char>(0x01));
    binary_raw.push_back(static_cast<char>(0xFF));
    db::Cell binary;
    binary.kind = db::ValueKind::Binary;
    binary.is_null = false;
    binary.text = util::Base64Encode(binary_raw.data(), binary_raw.size());

    std::vector<db::Cell> row{IntCell("1"), TextCell("O'Reilly"), NullCell(), binary};
    std::vector<db::Cell> row2{IntCell("2"), TextCell("<b>中文</b>"), TextCell("a,b"), NullCell()};

    {
        std::ostringstream out;
        io::ExportOptions options;
        options.format = io::ExportFormat::Csv;
        options.null_literal = "NULL";
        io::ExportWriter writer(out, options);
        writer.Begin(columns);
        writer.WriteRow(row);
        writer.WriteRow(row2);
        writer.Finish();
        const std::string text = out.str();
        Expect(text.find("id,name,note,data\r\n") == 0, "导出 CSV：表头");
        Expect(text.find("1,O'Reilly,NULL,Af8=\r\n") != std::string::npos,
               "导出 CSV：NULL 字面量与二进制");
        Expect(text.find("\"a,b\"") != std::string::npos, "导出 CSV：含逗号字段加引号");
        Expect(writer.rows() == 2 && writer.ok(), "导出 CSV：行数正确");
    }

    {
        std::ostringstream out;
        io::ExportOptions options;
        options.format = io::ExportFormat::Json;
        io::ExportWriter writer(out, options);
        writer.Begin(columns);
        writer.WriteRow(row);
        writer.WriteRow(row2);
        writer.Finish();
        const nlohmann::json parsed = nlohmann::json::parse(out.str());
        Expect(parsed.is_array() && parsed.size() == 2, "导出 JSON：合法数组");
        Expect(parsed[0]["id"].is_number_integer() && parsed[0]["id"] == 1, "导出 JSON：数字不带引号");
        Expect(parsed[0]["note"].is_null(), "导出 JSON：NULL → null");
        Expect(parsed[0]["name"] == "O'Reilly", "导出 JSON：文本转义");
        Expect(parsed[1]["name"] == "<b>中文</b>", "导出 JSON：UTF-8 原样保留");
    }

    {
        std::ostringstream out;
        io::ExportOptions options;
        options.format = io::ExportFormat::Sql;
        options.table_name = "main.users";
        io::ExportWriter writer(out, options);
        writer.Begin(columns);
        writer.WriteRow(row);
        writer.Finish();
        const std::string text = out.str();
        Expect(text.find("INSERT INTO \"main\".\"users\" (\"id\", \"name\", \"note\", \"data\")") !=
                   std::string::npos,
               "导出 SQL：限定表名与列名");
        Expect(text.find("(1, 'O''Reilly', NULL, X'01ff')") != std::string::npos,
               "导出 SQL：引号、NULL、二进制");
    }

    {
        std::ostringstream out;
        io::ExportOptions options;
        options.format = io::ExportFormat::Markdown;
        io::ExportWriter writer(out, options);
        writer.Begin(columns);
        writer.WriteRow(row2);
        writer.Finish();
        const std::string text = out.str();
        Expect(text.find("| id | name | note | data |") != std::string::npos, "导出 Markdown：表头");
        Expect(text.find("| --- |") != std::string::npos, "导出 Markdown：分隔行");
        Expect(text.find("<b>中文</b>") != std::string::npos, "导出 Markdown：内容");
    }

    {
        std::ostringstream out;
        io::ExportOptions options;
        options.format = io::ExportFormat::Html;
        io::ExportWriter writer(out, options);
        writer.Begin(columns);
        writer.WriteRow(row2);
        writer.Finish();
        const std::string text = out.str();
        Expect(text.find("<th>id</th>") != std::string::npos, "导出 HTML：表头");
        Expect(text.find("&lt;b&gt;中文&lt;/b&gt;") != std::string::npos, "导出 HTML：转义");
    }

    {
        std::ostringstream out;
        io::ExportOptions options;
        options.format = io::ExportFormat::Csv;
        options.encoding = io::TextEncoding::Gbk;
        io::ExportWriter writer(out, options);
        writer.Begin(columns);
        writer.WriteRow(row2);
        writer.Finish();
        std::string decoded;
        std::string error;
        Expect(io::DecodeToUtf8(out.str(), io::TextEncoding::Gbk, decoded, error) &&
                   decoded.find("中文") != std::string::npos,
               "导出 CSV：GBK 编码可还原");
    }
}

/// 丢弃所有字节的 streambuf，用于验证百万行导出不会在内存里堆积输出。
class DiscardBuf : public std::streambuf {
protected:
    std::streamsize xsputn(const char*, std::streamsize count) override {
        written_ += static_cast<std::uint64_t>(count);
        return count;
    }
    int_type overflow(int_type ch) override {
        if (!traits_type::eq_int_type(ch, traits_type::eof())) ++written_;
        return traits_type::not_eof(ch);
    }

public:
    std::uint64_t written() const { return written_; }

private:
    std::uint64_t written_ = 0;
};

void TestLargeExport() {
    std::vector<db::ColumnMeta> columns(6);
    columns[0].name = "id";
    columns[1].name = "name";
    columns[2].name = "email";
    columns[3].name = "status";
    columns[4].name = "score";
    columns[5].name = "created_at";

    DiscardBuf sink;
    std::ostream out(&sink);
    io::ExportOptions options;
    options.format = io::ExportFormat::Csv;

    io::ExportWriter writer(out, options);
    writer.Begin(columns);
    for (int i = 1; i <= 1000000; ++i) {
        std::vector<db::Cell> row;
        row.reserve(columns.size());
        row.push_back(IntCell(std::to_string(i)));
        row.push_back(TextCell("用户" + std::to_string(i)));
        row.push_back(TextCell("user" + std::to_string(i) + "@example.com"));
        row.push_back(TextCell("active"));
        row.push_back(IntCell("99.5"));
        row.push_back(TextCell("2026-11-14 12:00:00"));
        writer.WriteRow(row);
    }
    writer.Finish();
    Expect(writer.rows() == 1000000, "导出：100 万行计数");
    Expect(writer.ok(), "导出：100 万行无写入错误");
    Expect(sink.written() > 50ull * 1024 * 1024, "导出：100 万行输出超过 50MB（流式）");
}

void TestImport() {
    std::string error;
    auto connection = OpenMemory(error);
    Expect(connection != nullptr, "导入：打开内存库");
    if (!connection) return;

    // 10 万行导入
    constexpr int kRows = 100000;
    std::string text = "id,name\r\n";
    text.reserve(static_cast<std::size_t>(kRows) * 20);
    for (int i = 1; i <= kRows; ++i) {
        text += std::to_string(i);
        text += ",名称";
        text += std::to_string(i);
        text += "\r\n";
    }

    io::CsvImportOptions options;
    options.has_header = true;
    options.create_table = true;
    options.table = "bulk";
    options.batch_size = 500;

    io::CsvImportResult result;
    io::CsvImportError import_error;
    const io::ImportStatus status =
        io::ImportCsv(*connection, text, io::CsvOptions{}, options, result, import_error);
    Expect(status == io::ImportStatus::Ok, "导入：10 万行成功");
    Expect(result.rows == static_cast<std::uint64_t>(kRows), "导入：返回行数");
    Expect(CountRows(*connection, "\"bulk\"") == static_cast<std::uint64_t>(kRows),
           "导入：库中行数一致");

    // 错误回滚与行号定位
    Expect(Exec(*connection, "CREATE TABLE \"strict\" (id INTEGER PRIMARY KEY, name TEXT);")
               .error.code == 0,
           "导入：建严格表");
    std::string bad = "id,name\r\n";
    for (int i = 1; i <= 1000; ++i) {
        const int id = i == 700 ? 1 : i;
        bad += std::to_string(id) + ",row" + std::to_string(i) + "\r\n";
    }
    io::CsvImportOptions strict_options;
    strict_options.has_header = true;
    strict_options.table = "strict";
    strict_options.batch_size = 500;
    io::CsvImportResult strict_result;
    io::CsvImportError strict_error;
    const io::ImportStatus strict_status =
        io::ImportCsv(*connection, bad, io::CsvOptions{}, strict_options, strict_result, strict_error);
    Expect(strict_status == io::ImportStatus::Failed, "导入：错误数据失败");
    Expect(strict_error.rolled_back, "导入：失败后回滚");
    Expect(strict_error.record == 700, "导入：定位到第 700 条数据行");
    Expect(strict_error.line == 701, "导入：定位到文件第 701 行");
    Expect(CountRows(*connection, "\"strict\"") == 0, "导入：回滚后无残留");

    // 取消
    io::CsvImportOptions cancel_options;
    cancel_options.has_header = true;
    cancel_options.create_table = true;
    cancel_options.table = "cancelled";
    io::CsvImportResult cancel_result;
    io::CsvImportError cancel_error;
    const io::ImportStatus cancel_status =
        io::ImportCsv(*connection, text, io::CsvOptions{}, cancel_options, cancel_result, cancel_error,
                      {}, []() { return true; });
    Expect(cancel_status == io::ImportStatus::Cancelled, "导入：可取消");
    Expect(CountRows(*connection, "\"cancelled\"") == 0, "导入：取消后回滚");
}

void TestDump() {
    std::string error;
    auto source = OpenMemory(error);
    Expect(source != nullptr, "转储：打开源库");
    if (!source) return;

    Expect(Exec(*source, "CREATE TABLE \"users\" (id INTEGER PRIMARY KEY, name TEXT);").error.code == 0,
           "转储：建 users");
    Expect(Exec(*source, "CREATE TABLE \"notes\" (id INTEGER PRIMARY KEY, body TEXT);").error.code == 0,
           "转储：建 notes");
    Expect(Exec(*source, "INSERT INTO \"users\" (id, name) VALUES (1, '张三'), (2, 'O''Reilly');")
               .error.code == 0,
           "转储：写入 users");
    Expect(Exec(*source, "INSERT INTO \"notes\" (id, body) VALUES (1, 'line1\nline2');").error.code == 0,
           "转储：写入 notes");

    io::DumpOptions options;
    options.engine = db::Engine::Sqlite;
    options.scope = "main";
    options.tables = {"users", "notes"};
    options.include_structure = true;
    options.include_data = true;
    options.drop_if_exists = true;

    std::ostringstream out;
    io::DumpResult dump_result;
    const io::DumpStatus dump_status = io::DumpSql(*source, options, out, dump_result, error);
    Expect(dump_status == io::DumpStatus::Ok, "转储：执行成功");
    Expect(dump_result.tables == 2 && dump_result.rows == 3, "转储：表数/行数");
    const std::string script = out.str();
    Expect(script.find("DROP TABLE IF EXISTS") != std::string::npos, "转储：DROP IF EXISTS");
    Expect(script.find("CREATE TABLE") != std::string::npos, "转储：包含结构");
    Expect(script.find("INSERT INTO \"users\"") != std::string::npos, "转储：包含数据");

    // 空库恢复
    auto target = OpenMemory(error);
    Expect(target != nullptr, "转储：打开目标库");
    if (!target) return;
    const std::vector<db::SqlStatement> statements =
        db::SplitSql(script, db::SplitOptionsForEngine(0));
    bool executed = true;
    std::string failed_sql;
    for (const auto& statement : statements) {
        const db::ExecResult result = Exec(*target, statement.text);
        if (result.error.code != 0) {
            executed = false;
            failed_sql = statement.text + " => " + result.error.message;
            break;
        }
    }
    Expect(executed, "转储：可在空库执行" + (executed ? std::string{} : "（" + failed_sql + "）"));
    Expect(CountRows(*target, "\"users\"") == 2, "转储：恢复 users 行数");
    Expect(CountRows(*target, "\"notes\"") == 1, "转储：恢复 notes 行数");

    std::string name;
    db::ExecOptions exec_options;
    exec_options.fetch_size = 10;
    exec_options.max_cell_bytes = 0;
    target->Execute("SELECT name FROM \"users\" WHERE id = 2", {}, exec_options,
                    [&name](const db::RowBatch& batch) {
                        if (!batch.rows.empty() && !batch.rows.front().empty()) {
                            name = batch.rows.front().front().text;
                        }
                        return true;
                    });
    Expect(name == "O'Reilly", "转储：恢复的引号内容一致");
}

}  // namespace

int main() {
    std::printf("Silkit io tests\n");
    TestCsv();
    TestEncoding();
    TestExporter();
    TestLargeExport();
    TestImport();
    TestDump();

    std::printf("%d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
