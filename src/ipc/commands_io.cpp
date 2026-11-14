#include "ipc/commands_io.h"

#include "db/engine_dialect.h"
#include "db/identifier.h"
#include "db/session.h"
#include "db/sql_splitter.h"
#include "db/sql_util.h"
#include "io/csv.h"
#include "io/dumper.h"
#include "io/encoding.h"
#include "io/exporter.h"
#include "io/importer.h"
#include "ipc/commands_db_common.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace silkit::ipc {
namespace {

using dbutil::json;
using io::TextEncoding;

constexpr std::size_t kImportLimit = 256ull * 1024 * 1024;
constexpr std::size_t kSqlFileLimit = 512ull * 1024 * 1024;

std::string Text(const json& object, const char* key, const std::string& fallback = {}) {
    if (!object.is_object() || !object.contains(key) || object[key].is_null()) return fallback;
    const auto& value = object[key];
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number_integer()) return std::to_string(value.get<long long>());
    if (value.is_number_unsigned()) return std::to_string(value.get<unsigned long long>());
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    return fallback;
}

bool Flag(const json& object, const char* key, bool fallback = false) {
    if (!object.is_object() || !object.contains(key) || object[key].is_null()) return fallback;
    const auto& value = object[key];
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number_integer()) return value.get<long long>() != 0;
    if (value.is_string()) return value.get<std::string>() == "true" || value.get<std::string>() == "1";
    return fallback;
}

std::size_t SizeOption(const json& args, const json& settings, const char* arg_key,
                       const char* section, const char* setting_key, std::size_t fallback) {
    if (args.contains(arg_key) && args[arg_key].is_number_integer()) {
        const auto value = args[arg_key].get<long long>();
        if (value >= 0) return static_cast<std::size_t>(value);
    }
    if (settings.contains(section) && settings[section].is_object() &&
        settings[section].contains(setting_key) &&
        settings[section][setting_key].is_number_integer()) {
        const auto value = settings[section][setting_key].get<long long>();
        if (value >= 0) return static_cast<std::size_t>(value);
    }
    return fallback;
}

char DelimiterFrom(const std::string& value, char fallback) {
    if (value.empty() || value == "auto") return fallback;
    if (value == "\\t" || value == "tab" || value == "TAB") return '\t';
    return value.front();
}

std::string ScopeFrom(session::Session& session, const json& ref) {
    const db::Engine engine = session.Conn().EngineKind();
    std::string scope = Text(ref, "database");
    if (scope.empty()) scope = Text(ref, "schema");
    if (scope.empty() && engine == db::Engine::MySql) scope = session.CurrentDatabase();
    if (scope.empty() && engine == db::Engine::Postgres) scope = "public";
    if (scope.empty() && engine == db::Engine::Sqlite) scope = "main";
    return scope;
}

std::string Qualified(db::Engine engine, const std::string& scope, const std::string& table) {
    if (scope.empty()) return db::QuoteIdentifier(engine, table);
    return db::QuoteIdentifier(engine, scope) + "." + db::QuoteIdentifier(engine, table);
}

std::string ExtensionFormat(const std::string& path) {
    const std::string ext = std::filesystem::path(path).extension().string();
    if (ext == ".json") return "json";
    if (ext == ".sql") return "sql";
    if (ext == ".md" || ext == ".markdown") return "markdown";
    if (ext == ".html" || ext == ".htm") return "html";
    return "csv";
}

bool ReadFile(const std::string& path, std::size_t limit, std::string& out, std::string& error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        error = "无法读取文件: " + path;
        return false;
    }
    if (size > limit) {
        error = "文件超过 " + std::to_string(limit / (1024 * 1024)) + " MB 上限";
        return false;
    }
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) {
        error = "无法打开文件: " + path;
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (file.bad()) {
        error = "读取文件失败: " + path;
        return false;
    }
    return true;
}

/// 先写 <path>.part，成功后替换正式文件；析构时清理残留。
class PartFile {
public:
    explicit PartFile(std::string path)
        : final_path_(std::move(path)), part_path_(final_path_ + ".part") {}

    ~PartFile() { Discard(); }

    std::ofstream& stream() { return stream_; }

    bool Open(std::string& error) {
        stream_.open(std::filesystem::path(part_path_), std::ios::binary | std::ios::trunc);
        if (!stream_) {
            error = "无法写入文件: " + part_path_;
            return false;
        }
        return true;
    }

    bool Commit(std::string& error) {
        stream_.flush();
        if (!stream_) {
            error = "写入文件失败: " + part_path_;
            return false;
        }
        stream_.close();
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(final_path_), ec);
        std::filesystem::rename(std::filesystem::path(part_path_),
                                std::filesystem::path(final_path_), ec);
        if (ec) {
            error = "重命名临时文件失败: " + ec.message();
            return false;
        }
        committed_ = true;
        return true;
    }

    void Discard() {
        stream_.close();
        if (committed_) return;
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(part_path_), ec);
    }

    const std::string& final_path() const { return final_path_; }

private:
    std::string final_path_;
    std::string part_path_;
    std::ofstream stream_;
    bool committed_ = false;
};

TextEncoding EncodingFromArgs(const json& args, const json& settings, bool allow_auto,
                              bool& auto_detect, std::string& error) {
    auto_detect = false;
    std::string name = Text(args, "encoding");
    if (name.empty() && settings.contains("export") && settings["export"].is_object()) {
        name = Text(settings["export"], "defaultEncoding", "utf-8");
    }
    if (name.empty()) name = "utf-8";
    if (allow_auto && (name == "auto" || name == "自动")) {
        auto_detect = true;
        return TextEncoding::Utf8;
    }
    TextEncoding encoding = TextEncoding::Utf8;
    if (!io::EncodingFromName(name, encoding)) {
        error = "不支持的编码: " + name;
    }
    return encoding;
}

}  // namespace

void RegisterIoCommands(AsyncRouter& router, session::SessionManager& sessions, Settings& settings) {
    router.Register("export.result", [&sessions, &settings](const json& args, TaskContext& ctx) -> json {
        auto session = dbutil::RequireSession(sessions, Text(args, "sessionId"));
        const std::string path = Text(args, "path");
        if (path.empty()) dbutil::ThrowCommandError("bad_args", "缺少导出文件路径");

        const json snapshot = settings.Snapshot();
        const std::string format_name =
            Text(args, "format", ExtensionFormat(path));
        io::ExportFormat format = io::ExportFormat::Csv;
        if (!io::ExportFormatFromName(format_name, format)) {
            dbutil::ThrowCommandError("bad_args", "不支持的导出格式: " + format_name);
        }

        std::string error;
        bool auto_detect = false;
        const TextEncoding encoding = EncodingFromArgs(args, snapshot, false, auto_detect, error);
        if (!error.empty()) dbutil::ThrowCommandError("bad_args", error);

        io::ExportOptions options;
        options.format = format;
        options.csv.delimiter = DelimiterFrom(Text(args, "delimiter"), ',');
        if (args.contains("includeHeader")) {
            options.include_header = Flag(args, "includeHeader", true);
        } else if (snapshot.contains("export") && snapshot["export"].is_object() &&
                   snapshot["export"].contains("includeHeader")) {
            options.include_header = Flag(snapshot["export"], "includeHeader", true);
        }
        options.null_literal = Text(args, "nullLiteral", Text(snapshot.value("export", json::object()),
                                                              "nullLiteral", ""));
        options.encoding = encoding;

        const std::string sql = Text(args, "sql");
        const json ref = args.value("ref", json::object());
        const std::string requested_table = Text(args, "tableName", Text(ref, "name"));

        ctx.SetCancelHook([session] { session->Cancel(); });

        PartFile part(path);
        if (!part.Open(error)) dbutil::ThrowCommandError("io_error", error);

        json summary;
        try {
            summary = session->RunSync([&]() -> json {
                db::Connection& connection = session->Conn();
                options.engine = connection.EngineKind();

                std::string statement = sql;
                std::string table_name = requested_table;
                if (statement.empty()) {
                    const json table_ref = ref;
                    const std::string table = Text(table_ref, "name");
                    if (table.empty()) dbutil::ThrowCommandError("bad_args", "缺少导出 SQL 或表名");
                    const std::string scope = ScopeFrom(*session, table_ref);
                    statement = "SELECT * FROM " + Qualified(options.engine, scope, table) + ";";
                    if (table_name.empty()) table_name = table;
                }
                options.table_name = table_name;

                io::ExportWriter writer(part.stream(), options);
                db::ExecOptions exec_options;
                exec_options.fetch_size = 1000;
                exec_options.max_rows = 0;
                exec_options.max_cell_bytes = 0;

                bool began = false;
                const db::ExecResult result =
                    connection.Execute(statement, {}, exec_options, [&](const db::RowBatch& batch) {
                        if (ctx.IsCancelled()) return false;
                        if (!began) {
                            writer.Begin(batch.columns);
                            began = true;
                        }
                        for (const auto& row : batch.rows) writer.WriteRow(row);
                        ctx.progress(json{{"rows", writer.rows()}, {"bytes", writer.bytes()}});
                        return true;
                    });

                if (ctx.IsCancelled() || result.cancelled) {
                    return json{{"cancelled", true}};
                }
                if (result.error.code != 0) throw IpcError(dbutil::ErrorJson(result.error));

                if (!began && !result.columns.empty()) writer.Begin(result.columns);
                writer.Finish();
                if (!writer.ok()) dbutil::ThrowCommandError("io_error", "写入导出文件失败");
                return json{{"rows", writer.rows()},
                            {"bytes", writer.bytes()},
                            {"elapsedMs", result.elapsed_ms},
                            {"path", path},
                            {"format", io::ExportFormatName(format)},
                            {"encoding", io::EncodingName(encoding)}};
            });
        } catch (...) {
            part.Discard();
            throw;
        }

        if (summary.value("cancelled", false)) {
            part.Discard();
            return summary;
        }
        if (!part.Commit(error)) {
            part.Discard();
            dbutil::ThrowCommandError("io_error", error);
        }
        return summary;
    });

    router.Register("import.preview", [](const json& args, TaskContext&) -> json {
        const std::string path = Text(args, "path");
        if (path.empty()) dbutil::ThrowCommandError("bad_args", "缺少 CSV 文件路径");

        std::string bytes;
        std::string error;
        if (!ReadFile(path, kImportLimit, bytes, error)) dbutil::ThrowCommandError("io_error", error);

        TextEncoding encoding = io::DetectEncoding(bytes);
        const std::string requested = Text(args, "encoding");
        if (!requested.empty() && requested != "auto" && requested != "自动") {
            if (!io::EncodingFromName(requested, encoding)) {
                dbutil::ThrowCommandError("bad_args", "不支持的编码: " + requested);
            }
        }

        std::string text;
        if (!io::DecodeToUtf8(bytes, encoding, text, error)) {
            dbutil::ThrowCommandError("decode_error", error);
        }

        const std::string delimiter_arg = Text(args, "delimiter");
        const char delimiter = (delimiter_arg.empty() || delimiter_arg == "auto")
                                   ? io::DetectDelimiter(text, ',')
                                   : DelimiterFrom(delimiter_arg, ',');

        const bool has_header = Flag(args, "header", true);
        io::CsvOptions csv_options;
        csv_options.delimiter = delimiter;

        io::CsvParser parser(text, csv_options);
        std::vector<std::string> fields;
        json headers = json::array();
        json rows = json::array();
        std::size_t records = 0;
        std::size_t width = 0;
        while (parser.Next(fields)) {
            ++records;
            width = std::max(width, fields.size());
            if (records == 1 && has_header) {
                headers = fields;
                continue;
            }
            if (rows.size() < 20) {
                json row = json::array();
                for (const auto& field : fields) row.push_back(field);
                rows.push_back(std::move(row));
            }
            if (rows.size() >= 20) break;
        }
        if (parser.failed()) {
            dbutil::ThrowCommandError("parse_error", parser.error());
        }

        if (!has_header) {
            for (std::size_t i = 0; i < width; ++i) {
                headers.push_back("column" + std::to_string(i + 1));
            }
        }

        json columns = json::array();
        for (std::size_t i = 0; i < headers.size(); ++i) {
            columns.push_back(json{{"index", i},
                                   {"name", headers[i].is_string() ? headers[i].get<std::string>()
                                                                   : std::string{}}});
        }

        // 完整扫描一次统计总行数（预览只保留前 20 行）。
        std::size_t total_records = records;
        if (rows.size() >= 20) {
            io::CsvParser counter(text, csv_options);
            total_records = 0;
            while (counter.Next(fields)) ++total_records;
        }

        return json{{"path", path},
                    {"bytes", bytes.size()},
                    {"encoding", io::EncodingName(encoding)},
                    {"delimiter", std::string(1, delimiter)},
                    {"header", has_header},
                    {"headers", headers},
                    {"columns", columns},
                    {"rows", rows},
                    {"total", has_header ? (total_records > 0 ? total_records - 1 : 0) : total_records}};
    });

    router.Register("import.csv", [&sessions](const json& args, TaskContext& ctx) -> json {
        auto session = dbutil::RequireSession(sessions, Text(args, "sessionId"));
        const std::string path = Text(args, "path");
        if (path.empty()) dbutil::ThrowCommandError("bad_args", "缺少 CSV 文件路径");
        const std::string table = Text(args, "table");
        if (table.empty()) dbutil::ThrowCommandError("bad_args", "缺少目标表名");

        std::string bytes;
        std::string error;
        if (!ReadFile(path, kImportLimit, bytes, error)) dbutil::ThrowCommandError("io_error", error);

        TextEncoding encoding = io::DetectEncoding(bytes);
        const std::string requested = Text(args, "encoding");
        if (!requested.empty() && requested != "auto" && requested != "自动") {
            if (!io::EncodingFromName(requested, encoding)) {
                dbutil::ThrowCommandError("bad_args", "不支持的编码: " + requested);
            }
        }
        std::string text;
        if (!io::DecodeToUtf8(bytes, encoding, text, error)) {
            dbutil::ThrowCommandError("decode_error", error);
        }

        io::CsvOptions csv_options;
        csv_options.delimiter = DelimiterFrom(Text(args, "delimiter"), ',');
        if (Text(args, "delimiter") == "auto") {
            csv_options.delimiter = io::DetectDelimiter(text, ',');
        }

        io::CsvImportOptions options;
        options.has_header = Flag(args, "header", true);
        options.create_table = Flag(args, "createTable", false);
        options.empty_as_null = Flag(args, "emptyAsNull", true);
        options.batch_size = SizeOption(args, json::object(), "batchSize", "", "", 500);
        options.table = table;
        options.scope = Text(args, "scope");

        if (args.contains("columns") && args["columns"].is_array()) {
            for (const auto& item : args["columns"]) {
                io::CsvImportColumn column;
                if (item.contains("index") && item["index"].is_number_integer()) {
                    const auto index = item["index"].get<long long>();
                    if (index < 0) continue;
                    column.index = static_cast<std::size_t>(index);
                } else {
                    continue;
                }
                column.name = Text(item, "name");
                column.type = Text(item, "type");
                if (column.name.empty()) continue;
                options.columns.push_back(std::move(column));
            }
        }

        ctx.SetCancelHook([session] { session->Cancel(); });

        try {
            return session->RunSync([&]() -> json {
                if (options.scope.empty()) options.scope = ScopeFrom(*session, json::object());
                io::CsvImportResult result;
                io::CsvImportError import_error;
                const io::ImportStatus status =
                    io::ImportCsv(session->Conn(), text, csv_options, options, result, import_error,
                                  [&ctx](std::uint64_t rows, std::uint64_t total) {
                                      ctx.progress(json{{"rows", rows}, {"total", total}});
                                  },
                                  [&ctx]() { return ctx.IsCancelled(); });

                if (status == io::ImportStatus::Cancelled || ctx.IsCancelled()) {
                    return json{{"cancelled", true}};
                }
                if (status == io::ImportStatus::Failed) {
                    json payload{{"code", "import_failed"},
                                 {"message", import_error.message.empty() ? "导入失败"
                                                                         : import_error.message},
                                 {"stage", import_error.stage},
                                 {"record", import_error.record},
                                 {"line", import_error.line},
                                 {"rolledBack", import_error.rolled_back}};
                    throw IpcError(std::move(payload));
                }
                return json{{"rows", result.rows},
                            {"batches", result.batches},
                            {"elapsedMs", result.elapsed_ms},
                            {"table", options.table},
                            {"encoding", io::EncodingName(encoding)}};
            });
        } catch (...) {
            ctx.ClearCancelHook();
            throw;
        }
    });

    router.Register("dump.sql", [&sessions, &settings](const json& args, TaskContext& ctx) -> json {
        auto session = dbutil::RequireSession(sessions, Text(args, "sessionId"));
        const std::string path = Text(args, "path");
        if (path.empty()) dbutil::ThrowCommandError("bad_args", "缺少转储文件路径");

        io::DumpOptions options;
        options.scope = Text(args, "scope");
        options.include_structure = Flag(args, "includeStructure", true);
        options.include_data = Flag(args, "includeData", true);
        options.drop_if_exists = Flag(args, "dropIfExists", true);
        options.null_literal = Text(args, "nullLiteral");
        std::string error;
        bool auto_detect = false;
        options.encoding = EncodingFromArgs(args, settings.Snapshot(), false, auto_detect, error);
        if (!error.empty()) dbutil::ThrowCommandError("bad_args", error);
        if (args.contains("tables") && args["tables"].is_array()) {
            for (const auto& item : args["tables"]) {
                if (item.is_string() && !item.get<std::string>().empty()) {
                    options.tables.push_back(item.get<std::string>());
                }
            }
        }

        ctx.SetCancelHook([session] { session->Cancel(); });

        PartFile part(path);
        if (!part.Open(error)) dbutil::ThrowCommandError("io_error", error);

        json summary;
        try {
            summary = session->RunSync([&]() -> json {
                options.engine = session->Conn().EngineKind();
                if (options.scope.empty()) options.scope = ScopeFrom(*session, json::object());
                io::DumpResult result;
                const io::DumpStatus status =
                    io::DumpSql(session->Conn(), options, part.stream(), result, error,
                                [&ctx](std::size_t done, std::size_t total, const std::string& table,
                                       std::uint64_t rows) {
                                    ctx.progress(json{{"tableIndex", done},
                                                      {"total", total},
                                                      {"table", table},
                                                      {"rows", rows}});
                                },
                                [&ctx]() { return ctx.IsCancelled(); });

                if (status == io::DumpStatus::Cancelled || ctx.IsCancelled()) {
                    return json{{"cancelled", true}};
                }
                if (status == io::DumpStatus::Failed) {
                    dbutil::ThrowCommandError("dump_failed", error);
                }
                return json{{"tables", result.tables},
                            {"rows", result.rows},
                            {"bytes", result.bytes},
                            {"path", path},
                            {"encoding", io::EncodingName(options.encoding)}};
            });
        } catch (...) {
            part.Discard();
            ctx.ClearCancelHook();
            throw;
        }

        if (summary.value("cancelled", false)) {
            part.Discard();
            return summary;
        }
        if (!part.Commit(error)) {
            part.Discard();
            dbutil::ThrowCommandError("io_error", error);
        }
        return summary;
    });

    router.Register("query.execute_file", [&sessions, &settings](const json& args,
                                                                 TaskContext& ctx) -> json {
        auto session = dbutil::RequireSession(sessions, Text(args, "sessionId"));
        const std::string path = Text(args, "path");
        if (path.empty()) dbutil::ThrowCommandError("bad_args", "缺少 SQL 文件路径");
        const bool continue_on_error = Flag(args, "continueOnError", false);

        std::string bytes;
        std::string error;
        if (!ReadFile(path, kSqlFileLimit, bytes, error)) {
            dbutil::ThrowCommandError("io_error", error);
        }

        TextEncoding encoding = io::DetectEncoding(bytes);
        const std::string requested = Text(args, "encoding");
        if (!requested.empty() && requested != "auto" && requested != "自动") {
            if (!io::EncodingFromName(requested, encoding)) {
                dbutil::ThrowCommandError("bad_args", "不支持的编码: " + requested);
            }
        }
        std::string text;
        if (!io::DecodeToUtf8(bytes, encoding, text, error)) {
            dbutil::ThrowCommandError("decode_error", error);
        }

        const json settings_snapshot = settings.Snapshot();
        ctx.SetCancelHook([session] { session->Cancel(); });

        try {
            return session->RunSync([&]() -> json {
                db::Connection& connection = session->Conn();
                const db::SplitOptions split_options = db::SplitOptionsForEngine(
                    connection.EngineKind() == db::Engine::MySql
                        ? 1
                        : (connection.EngineKind() == db::Engine::Postgres ? 2 : 0));
                const std::vector<db::SqlStatement> statements = db::SplitSql(text, split_options);
                if (statements.empty()) dbutil::ThrowCommandError("empty_sql", "SQL 文件没有可执行的语句");
                dbutil::CheckSafeMode(args, settings_snapshot, statements);

                json summaries = json::array();
                json errors = json::array();
                const auto started = std::chrono::steady_clock::now();

                for (std::size_t i = 0; i < statements.size(); ++i) {
                    if (ctx.IsCancelled()) return json{{"cancelled", true}};
                    const auto& statement = statements[i];
                    ctx.emit("statement", json{{"index", i},
                                               {"total", statements.size()},
                                               {"offset", statement.begin_offset},
                                               {"sql", statement.text}});

                    db::ExecOptions options;
                    options.fetch_size = 500;
                    options.max_rows = 0;
                    options.max_cell_bytes = 262144;
                    std::uint64_t seen = 0;
                    const db::ExecResult result =
                        connection.Execute(statement.text, {}, options, [&](const db::RowBatch& batch) {
                            if (ctx.IsCancelled()) return false;
                            seen += batch.rows.size();
                            return true;
                        });

                    if (ctx.IsCancelled() || result.cancelled) {
                        return json{{"cancelled", true}};
                    }

                    if (result.error.code != 0) {
                        json payload = dbutil::ErrorJson(result.error);
                        payload["statementIndex"] = i;
                        payload["sql"] = statement.text;
                        payload["offset"] = statement.begin_offset;
                        if (!continue_on_error) throw IpcError(std::move(payload));
                        errors.push_back(payload);
                        summaries.push_back(json{{"index", i},
                                                  {"rows", 0},
                                                  {"affected", 0},
                                                  {"elapsedMs", result.elapsed_ms},
                                                  {"error", payload}});
                        continue;
                    }

                    const std::string keyword = db::FirstSqlKeyword(statement.text);
                    if (keyword == "BEGIN" || keyword == "START") {
                        session->SetTx(session::TxState::InTransaction);
                    } else if (keyword == "COMMIT" || keyword == "END" || keyword == "ROLLBACK") {
                        session->SetTx(session::TxState::Autocommit);
                    }

                    summaries.push_back(json{{"index", i},
                                             {"rows", result.rows == 0 ? seen : result.rows},
                                             {"affected", result.affected},
                                             {"elapsedMs", result.elapsed_ms}});
                    ctx.progress(json{{"index", i + 1}, {"total", statements.size()}});
                }

                const auto elapsed = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started)
                        .count());
                json response{{"statements", summaries}, {"elapsedMs", elapsed}};
                if (continue_on_error) response["errors"] = std::move(errors);
                return response;
            });
        } catch (...) {
            ctx.ClearCancelHook();
            throw;
        }
    });
}

}  // namespace silkit::ipc
