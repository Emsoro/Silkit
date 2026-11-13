#include "db/drivers/sqlite_driver.h"

#include "db/identifier.h"
#include "db/sql_util.h"
#include "db/type_map.h"
#include "util/base64.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace silkit::db {
namespace {

void SetError(DbError& err, int code, const std::string& message, int position = -1) {
    err.code = code;
    err.message = message;
    err.position = position;
}

std::string SafeText(const unsigned char* text, int bytes) {
    if (text == nullptr || bytes <= 0) return {};
    return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(bytes));
}

std::size_t SafeUtf8Cut(const char* data, std::size_t bytes, std::size_t limit) {
    if (bytes <= limit) return bytes;
    std::size_t cut = limit;
    while (cut > 0) {
        const auto ch = static_cast<unsigned char>(data[cut]);
        if ((ch & 0xC0) != 0x80) break;
        --cut;
    }
    return cut;
}

int SqliteErrorPosition(sqlite3* db) {
#if defined(SQLITE_VERSION_NUMBER) && SQLITE_VERSION_NUMBER >= 3038000
    const int offset = sqlite3_error_offset(db);
    return offset >= 0 ? offset + 1 : -1;
#else
    (void)db;
    return -1;
#endif
}

std::string UpperAscii(std::string text) {
    for (char& ch : text) {
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    }
    return text;
}

/// journal_mode 会直接拼进 PRAGMA，必须白名单，拒绝任意输入。
bool IsAllowedJournalMode(const std::string& mode) {
    const std::string upper = UpperAscii(mode);
    return upper == "DELETE" || upper == "TRUNCATE" || upper == "PERSIST" ||
           upper == "MEMORY" || upper == "WAL" || upper == "OFF";
}

/// sqlite3_prepare_v2 只编译第一条语句；多语句由上层 sql_splitter 拆分。
/// 这里判断 tail 是否还残留可执行内容，避免静默丢弃后续语句。
bool HasExecutableTail(const char* tail) {
    if (tail == nullptr) return false;
    const std::string text{tail};
    std::size_t i = 0;
    while (i < text.size()) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v' ||
            ch == ';') {
            ++i;
            continue;
        }
        if (ch == '-' && i + 1 < text.size() && text[i + 1] == '-') {
            i += 2;
            while (i < text.size() && text[i] != '\n') ++i;
            continue;
        }
        if (ch == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) ++i;
            i = (i + 1 < text.size()) ? i + 2 : text.size();
            continue;
        }
        return true;
    }
    return false;
}

bool BindValue(sqlite3_stmt* stmt, int index, const Param& param, std::string& error) {
    if (param.is_null || param.kind == ValueKind::Null) {
        sqlite3_bind_null(stmt, index);
        return true;
    }

    switch (param.kind) {
        case ValueKind::Int:
        case ValueKind::UInt:
        case ValueKind::Bool: {
            char* end = nullptr;
            const long long value = std::strtoll(param.text.c_str(), &end, 10);
            if (end != nullptr && *end == '\0') {
                sqlite3_bind_int64(stmt, index, value);
                return true;
            }
            break;
        }
        case ValueKind::Float: {
            char* end = nullptr;
            const double value = std::strtod(param.text.c_str(), &end);
            if (end != nullptr && *end == '\0') {
                sqlite3_bind_double(stmt, index, value);
                return true;
            }
            break;
        }
        case ValueKind::Binary: {
            std::string bytes;
            if (!util::Base64Decode(param.text, bytes)) {
                error = "二进制参数不是合法的 base64";
                return false;
            }
            sqlite3_bind_blob(stmt, index, bytes.data(), static_cast<int>(bytes.size()), SQLITE_TRANSIENT);
            return true;
        }
        default:
            break;
    }

    sqlite3_bind_text(stmt, index, param.text.c_str(), static_cast<int>(param.text.size()),
                      SQLITE_TRANSIENT);
    return true;
}

Cell MakeCell(sqlite3_stmt* stmt,
              int column,
              const std::string& decl_type,
              const ExecOptions& opt) {
    Cell cell;
    const int type = sqlite3_column_type(stmt, static_cast<int>(column));
    cell.kind = KindFromSqliteType(type, decl_type);

    if (type == SQLITE_NULL) {
        cell.is_null = true;
        cell.kind = ValueKind::Null;
        return cell;
    }

    cell.is_null = false;
    if (type == SQLITE_BLOB) {
        const auto* data = static_cast<const char*>(sqlite3_column_blob(stmt, column));
        const int bytes = sqlite3_column_bytes(stmt, column);
        cell.bytes = bytes > 0 ? static_cast<std::uint64_t>(bytes) : 0;
        const std::size_t limit = opt.max_cell_bytes == 0 ? static_cast<std::size_t>(bytes)
                                                          : opt.max_cell_bytes;
        const std::size_t used = bytes > 0 ? std::min(static_cast<std::size_t>(bytes), limit) : 0;
        cell.text = util::Base64Encode(data, used);
        cell.truncated = used < static_cast<std::size_t>(bytes);
        return cell;
    }

    if (type == SQLITE_INTEGER) {
        cell.text = std::to_string(sqlite3_column_int64(stmt, column));
        cell.bytes = cell.text.size();
        return cell;
    }
    if (type == SQLITE_FLOAT) {
        char buffer[64] = {};
        sqlite3_snprintf(static_cast<int>(sizeof(buffer)), buffer, "%!.15g",
                         sqlite3_column_double(stmt, column));
        cell.text = buffer;
        cell.bytes = cell.text.size();
        return cell;
    }

    const auto* text = sqlite3_column_text(stmt, column);
    const int bytes = sqlite3_column_bytes(stmt, column);
    cell.bytes = bytes > 0 ? static_cast<std::uint64_t>(bytes) : 0;
    if (text != nullptr && bytes > 0) {
        const std::size_t limit =
            opt.max_cell_bytes == 0 ? static_cast<std::size_t>(bytes) : opt.max_cell_bytes;
        if (static_cast<std::size_t>(bytes) > limit) {
            const std::size_t cut = SafeUtf8Cut(reinterpret_cast<const char*>(text),
                                                static_cast<std::size_t>(bytes), limit);
            cell.text.assign(reinterpret_cast<const char*>(text), cut);
            cell.truncated = true;
        } else {
            cell.text = SafeText(text, bytes);
        }
    }
    return cell;
}

class SqliteConnection final : public Connection {
public:
    SqliteConnection(sqlite3* db, ConnectParams params)
        : db_(db), params_(std::move(params)) {}

    ~SqliteConnection() override {
        if (db_ != nullptr) {
            sqlite3_close_v2(db_);
            db_ = nullptr;
        }
    }

    Engine EngineKind() const override { return Engine::Sqlite; }

    Capabilities Caps() const override {
        Capabilities caps;
        caps.databases = true;
        caps.schemas = false;
        caps.views = true;
        caps.routines = false;
        caps.triggers = true;
        caps.events = false;
        caps.sequences = false;
        caps.transactional_ddl = false;
        caps.explain = true;
        caps.cancel = true;
        caps.user_management = false;
        caps.table_comment = false;
        return caps;
    }

    bool Ping(DbError& err) override {
        sqlite3_stmt* stmt = nullptr;
        const int rc = sqlite3_prepare_v2(db_, "SELECT 1", -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            SetError(err, rc, sqlite3_errmsg(db_), SqliteErrorPosition(db_));
            return false;
        }
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_ROW && step != SQLITE_DONE) {
            SetError(err, step, sqlite3_errmsg(db_));
            return false;
        }
        return true;
    }

    ExecResult Execute(const std::string& sql,
                       const std::vector<Param>& params,
                       const ExecOptions& opt,
                       const RowSink& sink) override {
        ExecResult result;
        const auto started = std::chrono::steady_clock::now();

        if (params_.read_only && !IsReadOnlyStatement(sql)) {
            SetError(result.error, SQLITE_READONLY, "只读连接不允许执行写语句");
            return result;
        }

        sqlite3_stmt* stmt = nullptr;
        const char* tail = nullptr;
        const int prepare_rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, &tail);
        if (prepare_rc != SQLITE_OK) {
            SetError(result.error, sqlite3_extended_errcode(db_), sqlite3_errmsg(db_),
                     SqliteErrorPosition(db_));
            return result;
        }

        if (HasExecutableTail(tail)) {
            if (stmt != nullptr) sqlite3_finalize(stmt);
            SetError(result.error, SQLITE_MISUSE,
                     "一次 Execute 只允许一条 SQL，请用 sql_splitter 拆分多语句脚本");
            return result;
        }

        if (stmt == nullptr) {
            // 纯注释/空白
            return result;
        }

        const int parameter_count = sqlite3_bind_parameter_count(stmt);
        if (parameter_count != static_cast<int>(params.size())) {
            sqlite3_finalize(stmt);
            SetError(result.error, SQLITE_MISUSE,
                     "参数个数不匹配：SQL 需要 " + std::to_string(parameter_count) + " 个，实际传入 " +
                         std::to_string(params.size()) + " 个");
            return result;
        }

        std::string bind_error;
        for (int i = 1; i <= parameter_count; ++i) {
            if (!BindValue(stmt, i, params[static_cast<std::size_t>(i - 1)], bind_error)) {
                sqlite3_finalize(stmt);
                SetError(result.error, SQLITE_MISUSE, bind_error);
                return result;
            }
        }

        const int column_count = sqlite3_column_count(stmt);
        result.columns.reserve(static_cast<std::size_t>(column_count));
        for (int i = 0; i < column_count; ++i) {
            ColumnMeta column;
            const char* name = sqlite3_column_name(stmt, i);
            const char* decl = sqlite3_column_decltype(stmt, i);
            column.name = name != nullptr ? name : "";
            column.db_type = decl != nullptr ? decl : "";
            column.kind = KindFromDeclType(column.db_type);
            result.columns.push_back(std::move(column));
        }

        const std::size_t fetch_size = opt.fetch_size == 0 ? 500 : opt.fetch_size;
        RowBatch batch;
        std::size_t batch_rows = 0;
        bool first_batch = true;
        bool stop = false;

        for (;;) {
            const int step = sqlite3_step(stmt);
            if (step == SQLITE_ROW) {
                if (opt.max_rows > 0 && result.rows >= opt.max_rows) {
                    result.truncated = true;
                    break;
                }

                std::vector<Cell> row;
                row.reserve(static_cast<std::size_t>(column_count));
                for (int i = 0; i < column_count; ++i) {
                    row.push_back(MakeCell(stmt, i, result.columns[static_cast<std::size_t>(i)].db_type, opt));
                    if (result.columns[static_cast<std::size_t>(i)].kind == ValueKind::Unknown ||
                        result.columns[static_cast<std::size_t>(i)].kind == ValueKind::Null) {
                        result.columns[static_cast<std::size_t>(i)].kind =
                            row.back().kind == ValueKind::Null ? ValueKind::Text : row.back().kind;
                    }
                }
                batch.rows.push_back(std::move(row));
                ++result.rows;
                ++batch_rows;

                if (batch_rows >= fetch_size) {
                    if (first_batch) {
                        batch.columns = result.columns;
                        first_batch = false;
                    }
                    if (sink && !sink(batch)) {
                        result.truncated = true;
                        stop = true;
                        break;
                    }
                    batch = RowBatch{};
                    batch_rows = 0;
                }
                continue;
            }

            if (step == SQLITE_DONE) break;

            result.error.code = sqlite3_extended_errcode(db_);
            result.error.message = sqlite3_errmsg(db_);
            result.error.position = SqliteErrorPosition(db_);
            result.cancelled = result.error.code == SQLITE_INTERRUPT;
            break;
        }

        if (!stop && !batch.rows.empty() && sink) {
            if (first_batch) {
                batch.columns = result.columns;
                first_batch = false;
            }
            if (!sink(batch)) result.truncated = true;
        }

        // 空结果集也要把列元数据送到前端（否则网格没有表头）。
        if (result.error.code == 0 && column_count > 0 && first_batch && sink) {
            batch.columns = result.columns;
            sink(batch);
        }

        sqlite3_finalize(stmt);

        if (result.error.code == 0 && column_count == 0) {
            result.affected = static_cast<std::uint64_t>(sqlite3_changes(db_));
            result.last_insert_id = static_cast<std::uint64_t>(sqlite3_last_insert_rowid(db_));
        }
        result.elapsed_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                  started)
                .count());
        return result;
    }

    void Cancel() override { sqlite3_interrupt(db_); }

    void SetReadOnly(bool read_only) override { params_.read_only = read_only; }
    bool IsReadOnly() const override { return params_.read_only; }

    std::string ServerVersion() const override { return sqlite3_libversion(); }

    std::string CurrentDatabase() const override { return "main"; }

    std::uint64_t ThreadId() const override { return 0; }

    std::string Placeholder(std::size_t) const override { return "?"; }

private:
    sqlite3* db_ = nullptr;
    ConnectParams params_;
};

}  // namespace

std::unique_ptr<Connection> SqliteDriver::Open(const ConnectParams& params, DbError& err) {
    if (params.file.empty()) {
        SetError(err, SQLITE_CANTOPEN, "未指定 SQLite 文件路径");
        return nullptr;
    }

    const std::string journal_mode = UpperAscii(params.journal_mode);
    if (!journal_mode.empty() && !IsAllowedJournalMode(journal_mode)) {
        SetError(err, SQLITE_MISUSE, "不支持的 journal_mode: " + params.journal_mode);
        return nullptr;
    }

    int flags = params.read_only ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    // sqlite3_interrupt 会从 UI 线程跨线程调用，显式要求 serialized 模式。
    flags |= SQLITE_OPEN_URI | SQLITE_OPEN_FULLMUTEX;

    sqlite3* db = nullptr;
    const int rc = sqlite3_open_v2(params.file.c_str(), &db, flags, nullptr);
    if (rc != SQLITE_OK) {
        const std::string message = db != nullptr ? sqlite3_errmsg(db) : "无法打开 SQLite 数据库";
        if (db != nullptr) sqlite3_close_v2(db);
        SetError(err, rc, message);
        return nullptr;
    }

    sqlite3_extended_result_codes(db, 1);
    if (params.busy_timeout_ms > 0) sqlite3_busy_timeout(db, params.busy_timeout_ms);

    auto exec_sql = [db](const std::string& sql, std::string& message) -> bool {
        char* error = nullptr;
        const int code = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);
        if (code != SQLITE_OK) {
            message = error != nullptr ? error : sqlite3_errmsg(db);
            if (error != nullptr) sqlite3_free(error);
            return false;
        }
        return true;
    };

    std::string sql_error;
    if (!exec_sql(std::string("PRAGMA foreign_keys = ") + (params.foreign_keys ? "ON" : "OFF"),
                  sql_error)) {
        sqlite3_close_v2(db);
        SetError(err, SQLITE_ERROR, sql_error);
        return nullptr;
    }

    if (!params.read_only && !journal_mode.empty() &&
        !exec_sql("PRAGMA journal_mode = " + journal_mode, sql_error)) {
        sqlite3_close_v2(db);
        SetError(err, SQLITE_ERROR, sql_error);
        return nullptr;
    }

    // init_sql 在连接建立后、交给上层使用前执行；失败则不交付这个连接。
    if (!params.init_sql.empty() && !exec_sql(params.init_sql, sql_error)) {
        sqlite3_close_v2(db);
        SetError(err, SQLITE_ERROR, "初始 SQL 执行失败：" + sql_error);
        return nullptr;
    }

    return std::make_unique<SqliteConnection>(db, params);
}

}  // namespace silkit::db
