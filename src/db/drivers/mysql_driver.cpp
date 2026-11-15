#include "db/drivers/mysql_driver.h"

#include "db/engine_dialect.h"
#include "db/sql_util.h"
#include "db/type_map.h"
#include "util/base64.h"

#include <mysql.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace silkit::db {
namespace {

std::once_flag g_library_once;

std::string CutUtf8(const std::string& text, std::size_t limit) {
    if (text.size() <= limit) return text;
    std::size_t end = limit;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
    return text.substr(0, end);
}

std::string FieldTypeName(MYSQL_FIELD* field) {
    const unsigned long length = field->length;
    const bool unsigned_flag = (field->flags & UNSIGNED_FLAG) != 0;
    switch (field->type) {
    case MYSQL_TYPE_TINY: return std::string("tinyint") + (unsigned_flag ? " unsigned" : "");
    case MYSQL_TYPE_SHORT: return std::string("smallint") + (unsigned_flag ? " unsigned" : "");
    case MYSQL_TYPE_LONG: return std::string("int") + (unsigned_flag ? " unsigned" : "");
    case MYSQL_TYPE_INT24: return std::string("mediumint") + (unsigned_flag ? " unsigned" : "");
    case MYSQL_TYPE_LONGLONG: return std::string("bigint") + (unsigned_flag ? " unsigned" : "");
    case MYSQL_TYPE_DECIMAL:
    case MYSQL_TYPE_NEWDECIMAL: return "decimal(" + std::to_string(length) + ",?)";
    case MYSQL_TYPE_FLOAT: return "float";
    case MYSQL_TYPE_DOUBLE: return "double";
    case MYSQL_TYPE_BIT: return "bit(" + std::to_string(length) + ")";
    case MYSQL_TYPE_DATE: return "date";
    case MYSQL_TYPE_NEWDATE: return "date";
    case MYSQL_TYPE_TIME: return "time";
    case MYSQL_TYPE_TIMESTAMP: return "timestamp";
    case MYSQL_TYPE_DATETIME: return "datetime";
    case MYSQL_TYPE_YEAR: return "year";
    case MYSQL_TYPE_JSON: return "json";
    case MYSQL_TYPE_ENUM: return "enum(...)";
    case MYSQL_TYPE_SET: return "set(...)";
    case MYSQL_TYPE_TINY_BLOB: return "tinytext";
    case MYSQL_TYPE_MEDIUM_BLOB: return "mediumtext";
    case MYSQL_TYPE_LONG_BLOB: return "longtext";
    case MYSQL_TYPE_BLOB: return "text";
    case MYSQL_TYPE_GEOMETRY: return "geometry";
    case MYSQL_TYPE_NULL: return "null";
    default:
        return "varchar(" + std::to_string(length) + ")";
    }
}

bool IsBinaryStringType(int field_type) {
    // charsetnr == 63 仅对真正的字符串/BLOB 类型表示「二进制」；
    // 数值/日期/时间/枚举/集合/JSON 列的字符集本就是 63，不能据此判为二进制。
    return field_type == MYSQL_TYPE_STRING || field_type == MYSQL_TYPE_VAR_STRING ||
           field_type == MYSQL_TYPE_BLOB || field_type == MYSQL_TYPE_TINY_BLOB ||
           field_type == MYSQL_TYPE_MEDIUM_BLOB || field_type == MYSQL_TYPE_LONG_BLOB ||
           field_type == MYSQL_TYPE_GEOMETRY;
}

bool IsBinaryField(MYSQL_FIELD* field) {
    // information_schema 的 longtext 列虽以 BLOB 类型返回且字符集非 binary，应作文本；
    // 真正的 BLOB/VARBINARY/BINARY 才判为二进制。
    if (field->charsetnr != 63) return false;
    return IsBinaryStringType(field->type);
}

Cell MakeCell(const char* data, unsigned long length, ValueKind kind, bool binary,
              std::size_t max_bytes) {
    Cell cell;
    if (data == nullptr) return cell;
    cell.is_null = false;
    cell.kind = kind;
    cell.bytes = length;
    if (binary) {
        std::string encoded = util::Base64Encode(data, length);
        if (max_bytes > 0 && encoded.size() > max_bytes) {
            encoded = CutUtf8(encoded, max_bytes);
            cell.truncated = true;
        }
        cell.text = std::move(encoded);
        return cell;
    }
    std::string text(data, length);
    if (max_bytes > 0 && text.size() > max_bytes) {
        text = CutUtf8(text, max_bytes);
        cell.truncated = true;
    }
    cell.text = std::move(text);
    return cell;
}

void FillError(MYSQL* handle, DbError& error) {
    error.code = static_cast<int>(mysql_errno(handle));
    const char* state = mysql_sqlstate(handle);
    error.sql_state = state ? state : "";
    const char* message = mysql_error(handle);
    error.message = message ? message : "MySQL 连接错误";
    DecorateError(Engine::MySql, error);
}

void FillStmtError(MYSQL_STMT* stmt, DbError& error) {
    error.code = static_cast<int>(mysql_stmt_errno(stmt));
    const char* state = mysql_stmt_sqlstate(stmt);
    error.sql_state = state ? state : "";
    const char* message = mysql_stmt_error(stmt);
    error.message = message ? message : "MySQL 语句执行失败";
    DecorateError(Engine::MySql, error);
}

std::string ColumnDecl(MYSQL_FIELD* field) { return FieldTypeName(field); }

class MySqlConnection final : public Connection {
public:
    MySqlConnection(MYSQL* handle, ConnectParams params)
        : mysql_(handle), params_(std::move(params)) {
        thread_id_ = static_cast<std::uint64_t>(mysql_thread_id(mysql_));
    }

    ~MySqlConnection() override {
        if (mysql_) mysql_close(mysql_);
    }

    Engine EngineKind() const override { return Engine::MySql; }

    Capabilities Caps() const override {
        Capabilities caps;
        caps.databases = true;
        caps.routines = true;
        caps.events = true;
        caps.user_management = true;
        caps.table_comment = true;
        caps.transactional_ddl = false;
        return caps;
    }

    bool Ping(DbError& err) override {
        if (mysql_ping(mysql_) != 0) {
            FillError(mysql_, err);
            return false;
        }
        return true;
    }

    ExecResult Execute(const std::string& sql, const std::vector<Param>& params,
                       const ExecOptions& opt, const RowSink& sink) override {
        ExecResult result;
        const auto started = std::chrono::steady_clock::now();
        if (read_only_ && !IsReadOnlyStatement(sql)) {
            result.error.code = -1;
            result.error.message = "只读连接不允许执行写语句";
            result.elapsed_ms = Elapsed(started);
            return result;
        }
        if (params.empty()) {
            ExecuteText(sql, opt, sink, result);
        } else {
            ExecutePrepared(sql, params, opt, sink, result);
        }
        result.elapsed_ms = Elapsed(started);
        return result;
    }

    void Cancel() override {
        // KILL QUERY 必须用另一条控制连接，且可从任意线程调用。
        std::lock_guard<std::mutex> lock(cancel_mutex_);
        const std::uint64_t thread_id = thread_id_;
        if (thread_id == 0) return;

        MYSQL* killer = mysql_init(nullptr);
        if (!killer) return;
        unsigned int timeout = 2;
        mysql_options(killer, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
        unsigned int read_timeout = 2;
        mysql_options(killer, MYSQL_OPT_READ_TIMEOUT, &read_timeout);
        const unsigned int port = params_.port.empty()
                                      ? std::strtoul(DefaultPort(Engine::MySql), nullptr, 10)
                                      : std::strtoul(params_.port.c_str(), nullptr, 10);
        const char* db = params_.database.empty() ? nullptr : params_.database.c_str();
        if (mysql_real_connect(killer, params_.host.c_str(), params_.user.c_str(),
                               params_.password.c_str(), db, port, nullptr, 0) != nullptr) {
            char statement[64];
            std::snprintf(statement, sizeof(statement), "KILL QUERY %llu",
                          static_cast<unsigned long long>(thread_id));
            mysql_real_query(killer, statement, static_cast<unsigned long>(std::strlen(statement)));
        }
        mysql_close(killer);
    }

    void SetReadOnly(bool read_only) override { read_only_ = read_only; }
    bool IsReadOnly() const override { return read_only_; }

    std::string ServerVersion() const override {
        const char* version = mysql_get_server_info(mysql_);
        return version ? version : "";
    }

    std::string CurrentDatabase() const override {
        if (mysql_real_query(mysql_, "SELECT DATABASE()", 17) != 0) return params_.database;
        MYSQL_RES* res = mysql_store_result(mysql_);
        std::string value;
        if (res) {
            if (MYSQL_ROW row = mysql_fetch_row(res)) {
                if (row[0]) value = row[0];
            }
            mysql_free_result(res);
        }
        return value;
    }

    std::uint64_t ThreadId() const override { return thread_id_; }
    std::string Placeholder(std::size_t) const override { return "?"; }

private:
    static std::uint64_t Elapsed(const std::chrono::steady_clock::time_point& started) {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count());
    }

    bool CancelledByServer(int code) const {
        return code == 1317 || code == 1028 || code == 2006 || code == 2013;
    }

    void ExecuteText(const std::string& sql, const ExecOptions& opt, const RowSink& sink,
                      ExecResult& result) {
        if (mysql_real_query(mysql_, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0) {
            FillError(mysql_, result.error);
            result.cancelled = CancelledByServer(result.error.code);
            return;
        }
        MYSQL_RES* res = mysql_use_result(mysql_);
        if (!res) {
            if (mysql_field_count(mysql_) != 0) {
                FillError(mysql_, result.error);
                return;
            }
            result.affected = static_cast<std::uint64_t>(mysql_affected_rows(mysql_));
            result.last_insert_id = static_cast<std::uint64_t>(mysql_insert_id(mysql_));
            result.info = mysql_info(mysql_) ? mysql_info(mysql_) : "";
            return;
        }

        const unsigned int count = mysql_num_fields(res);
        MYSQL_FIELD* fields = mysql_fetch_fields(res);
        std::vector<ValueKind> kinds(count);
        std::vector<bool> binary(count);
        for (unsigned int i = 0; i < count; ++i) {
            ColumnMeta meta;
            meta.name = fields[i].name ? fields[i].name : "";
            meta.db_type = ColumnDecl(&fields[i]);
            const bool real_binary =
                (fields[i].charsetnr == 63) && IsBinaryStringType(fields[i].type);
            meta.kind = real_binary ? ValueKind::Binary
                                    : KindFromMySqlFieldType(fields[i].type, fields[i].flags,
                                                            meta.db_type);
            meta.nullable = (fields[i].flags & NOT_NULL_FLAG) == 0;
            meta.primary_key = (fields[i].flags & PRI_KEY_FLAG) != 0;
            meta.auto_increment = (fields[i].flags & AUTO_INCREMENT_FLAG) != 0;
            meta.generated = false;
            result.columns.push_back(std::move(meta));
            kinds[i] = result.columns.back().kind;
            binary[i] = real_binary;
        }

        RowBatch batch;
        batch.columns = result.columns;  // 首批带列元数据
        bool first_batch = true;
        std::size_t emitted = 0;
        bool stop = false;
        while (!stop) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (!row) break;
            const unsigned long* lengths = mysql_fetch_lengths(res);
            std::vector<Cell> cells(count);
            for (unsigned int i = 0; i < count; ++i) {
                cells[i] = MakeCell(row[i], lengths ? lengths[i] : 0, kinds[i], binary[i],
                                    opt.max_cell_bytes);
            }
            batch.rows.push_back(std::move(cells));
            ++emitted;
            const bool limit_reached = opt.max_rows > 0 && emitted >= opt.max_rows;
            if (batch.rows.size() >= opt.fetch_size || limit_reached) {
                if (sink && !sink(batch)) stop = true;
                result.rows += batch.rows.size();
                batch.rows.clear();
                batch.columns.clear();
                first_batch = false;
                if (limit_reached) {
                    result.truncated = true;
                    stop = true;
                }
            }
        }
        if (!stop && !batch.rows.empty() && sink) {
            result.rows += batch.rows.size();
            sink(batch);
        } else if (first_batch && result.rows == 0 && sink) {
            // 空结果集也要发一次列元数据
            sink(batch);
        }
        // 必须排空结果集，否则连接处于 "commands out of sync" 状态。
        if (result.truncated) {
            while (mysql_fetch_row(res)) {}
        }
        mysql_free_result(res);
        result.affected = static_cast<std::uint64_t>(mysql_affected_rows(mysql_));
    }

    void ExecutePrepared(const std::string& sql, const std::vector<Param>& params,
                          const ExecOptions& opt, const RowSink& sink, ExecResult& result) {
        MYSQL_STMT* stmt = mysql_stmt_init(mysql_);
        if (!stmt) {
            FillError(mysql_, result.error);
            return;
        }
        if (mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0) {
            FillStmtError(stmt, result.error);
            mysql_stmt_close(stmt);
            return;
        }
        const std::size_t expected = mysql_stmt_param_count(stmt);
        if (expected != params.size()) {
            result.error.code = -1;
            result.error.message = "参数个数不匹配：需要 " + std::to_string(expected) +
                                   "，收到 " + std::to_string(params.size());
            mysql_stmt_close(stmt);
            return;
        }

        std::vector<std::string> texts(params.size());
        std::vector<unsigned long> lengths(params.size(), 0);
        std::vector<my_bool> is_null(params.size(), 0);
        std::vector<MYSQL_BIND> binds(params.size());
        for (std::size_t i = 0; i < params.size(); ++i) {
            texts[i] = params[i].is_null ? std::string{} : params[i].text;
            if (params[i].kind == ValueKind::Binary && !params[i].is_null) {
                std::string decoded;
                if (util::Base64Decode(params[i].text, decoded)) texts[i] = std::move(decoded);
            }
            lengths[i] = static_cast<unsigned long>(texts[i].size());
            is_null[i] = params[i].is_null ? 1 : 0;
            auto& bind = binds[i];
            std::memset(&bind, 0, sizeof(bind));
            bind.buffer_type = params[i].is_null ? MYSQL_TYPE_NULL : MYSQL_TYPE_STRING;
            bind.buffer = params[i].is_null ? nullptr : const_cast<char*>(texts[i].data());
            bind.buffer_length = static_cast<unsigned long>(texts[i].size());
            bind.length = &lengths[i];
            bind.is_null = &is_null[i];
        }
        if (mysql_stmt_bind_param(stmt, binds.data()) != 0 ||
            mysql_stmt_execute(stmt) != 0) {
            FillStmtError(stmt, result.error);
            result.cancelled = CancelledByServer(result.error.code);
            mysql_stmt_close(stmt);
            return;
        }

        MYSQL_RES* metadata = mysql_stmt_result_metadata(stmt);
        if (!metadata) {
            result.affected = static_cast<std::uint64_t>(mysql_stmt_affected_rows(stmt));
            result.last_insert_id = static_cast<std::uint64_t>(mysql_stmt_insert_id(stmt));
            mysql_stmt_close(stmt);
            return;
        }

        const unsigned int count = mysql_num_fields(metadata);
        MYSQL_FIELD* fields = mysql_fetch_fields(metadata);
        std::vector<ValueKind> kinds(count);
        std::vector<bool> binary(count);
        std::vector<std::string> buffers(count);
        std::vector<unsigned long> out_lengths(count, 0);
        std::vector<my_bool> out_null(count, 0);
        std::vector<my_bool> out_error(count, 0);
        std::vector<MYSQL_BIND> out_binds(count);
        const std::size_t buffer_size = opt.max_cell_bytes > 0 ? opt.max_cell_bytes : 262144;
        for (unsigned int i = 0; i < count; ++i) {
            ColumnMeta meta;
            meta.name = fields[i].name ? fields[i].name : "";
            meta.db_type = ColumnDecl(&fields[i]);
            const bool real_binary =
                (fields[i].charsetnr == 63) && IsBinaryStringType(fields[i].type);
            meta.kind = real_binary ? ValueKind::Binary
                                    : KindFromMySqlFieldType(fields[i].type, fields[i].flags,
                                                            meta.db_type);
            meta.nullable = (fields[i].flags & NOT_NULL_FLAG) == 0;
            meta.primary_key = (fields[i].flags & PRI_KEY_FLAG) != 0;
            meta.auto_increment = (fields[i].flags & AUTO_INCREMENT_FLAG) != 0;
            result.columns.push_back(std::move(meta));
            kinds[i] = result.columns.back().kind;
            binary[i] = real_binary;
            buffers[i].resize(buffer_size, '\0');
            std::memset(&out_binds[i], 0, sizeof(out_binds[i]));
            out_binds[i].buffer_type = MYSQL_TYPE_STRING;
            out_binds[i].buffer = buffers[i].data();
            out_binds[i].buffer_length = static_cast<unsigned long>(buffer_size);
            out_binds[i].length = &out_lengths[i];
            out_binds[i].is_null = &out_null[i];
            out_binds[i].error = &out_error[i];
        }
        mysql_free_result(metadata);

        if (mysql_stmt_bind_result(stmt, out_binds.data()) != 0 ||
            mysql_stmt_store_result(stmt) != 0) {
            FillStmtError(stmt, result.error);
            mysql_stmt_close(stmt);
            return;
        }

        RowBatch batch;
        batch.columns = result.columns;
        std::size_t emitted = 0;
        bool stop = false;
        while (!stop) {
            for (unsigned int i = 0; i < count; ++i) {
                out_null[i] = 0;
                out_error[i] = 0;
                out_lengths[i] = 0;
                std::fill(buffers[i].begin(), buffers[i].end(), '\0');
            }
            const int status = mysql_stmt_fetch(stmt);
            if (status == MYSQL_NO_DATA) break;
            if (status != 0 && status != MYSQL_DATA_TRUNCATED) {
                FillStmtError(stmt, result.error);
                break;
            }
            std::vector<Cell> cells(count);
            for (unsigned int i = 0; i < count; ++i) {
                if (out_null[i]) continue;
                const unsigned long length = std::min<unsigned long>(
                    out_lengths[i], static_cast<unsigned long>(buffer_size));
                cells[i] = MakeCell(buffers[i].data(), length, kinds[i], binary[i],
                                    opt.max_cell_bytes);
                if (out_error[i] || out_lengths[i] > buffer_size) cells[i].truncated = true;
                cells[i].bytes = out_lengths[i];
            }
            batch.rows.push_back(std::move(cells));
            ++emitted;
            const bool limit_reached = opt.max_rows > 0 && emitted >= opt.max_rows;
            if (batch.rows.size() >= opt.fetch_size || limit_reached) {
                if (sink && !sink(batch)) stop = true;
                result.rows += batch.rows.size();
                batch.rows.clear();
                batch.columns.clear();
                if (limit_reached) {
                    result.truncated = true;
                    stop = true;
                }
            }
        }
        if (!stop && !batch.rows.empty() && sink) {
            result.rows += batch.rows.size();
            sink(batch);
        } else if (result.rows == 0 && sink) {
            sink(batch);  // 空结果集补发列元数据
        }
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
    }

    MYSQL* mysql_;
    ConnectParams params_;
    std::uint64_t thread_id_ = 0;
    bool read_only_ = false;
    std::mutex cancel_mutex_;
};

}  // namespace

std::unique_ptr<Connection> MySqlDriver::Open(const ConnectParams& params, DbError& err) {
    std::call_once(g_library_once, [] { mysql_library_init(0, nullptr, nullptr); });

    MYSQL* handle = mysql_init(nullptr);
    if (!handle) {
        err.code = -1;
        err.message = "无法初始化 MariaDB/MySQL 客户端";
        return nullptr;
    }

    const std::string charset = params.charset.empty() ? "utf8mb4" : params.charset;
    const unsigned int connect_timeout =
        static_cast<unsigned int>(std::max(1, params.connect_timeout_ms / 1000));
    my_bool no_reconnect = 0;
    mysql_options(handle, MYSQL_SET_CHARSET_NAME, charset.c_str());
    mysql_options(handle, MYSQL_OPT_CONNECT_TIMEOUT, &connect_timeout);
    mysql_options(handle, MYSQL_OPT_RECONNECT, &no_reconnect);
    if (!params.init_sql.empty()) {
        mysql_options(handle, MYSQL_INIT_COMMAND, params.init_sql.c_str());
    }
    if (!params.ssl_ca.empty() || !params.ssl_cert.empty() || !params.ssl_key.empty()) {
        mysql_ssl_set(handle,
                      params.ssl_key.empty() ? nullptr : params.ssl_key.c_str(),
                      params.ssl_cert.empty() ? nullptr : params.ssl_cert.c_str(),
                      params.ssl_ca.empty() ? nullptr : params.ssl_ca.c_str(),
                      nullptr, nullptr);
    }
    // MariaDB Connector/C 3.4 起默认要求校验服务端证书（tls_allow_invalid_server_cert=0），
    // 认证阶段会无条件把 options.use_ssl 置 1；此时单独设置 MYSQL_OPT_SSL_ENFORCE=0 无效，
    // 对不声明 CLIENT_SSL 的服务端（如 MySQL 协议代理）仍报 2026
    // "SSL is required, but the server does not support it"。
    // 因此必须显式写 MYSQL_OPT_SSL_VERIFY_SERVER_CERT：
    //   prefer           -> enforce=1, verify=0：可用则加密，服务端不支持时明文回退
    //   disable          -> enforce=0, verify=0：始终明文
    //   require/verify-* -> enforce=1, verify=1：必须 TLS，服务端不支持时明确报错
    // 注意 mysql_ssl_set() 内部会把 enforce 置 1，故 SSL 选项必须在它之后设置。
    const bool strict_tls = params.ssl_mode == "require" ||
                            params.ssl_mode == "verify-ca" ||
                            params.ssl_mode == "verify-full";
    my_bool verify_server_cert = strict_tls ? 1 : 0;
    mysql_options(handle, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &verify_server_cert);
    my_bool enforce = params.ssl_mode == "disable" ? 0 : 1;
    mysql_options(handle, MYSQL_OPT_SSL_ENFORCE, &enforce);

    const unsigned int port = static_cast<unsigned int>(
        params.port.empty() ? std::strtoul(DefaultPort(Engine::MySql), nullptr, 10)
                            : std::strtoul(params.port.c_str(), nullptr, 10));
    const char* database = params.database.empty() ? nullptr : params.database.c_str();
    if (mysql_real_connect(handle,
                            params.host.empty() ? "127.0.0.1" : params.host.c_str(),
                            params.user.empty() ? "" : params.user.c_str(),
                            params.password.c_str(), database, port, nullptr, 0) == nullptr) {
        FillError(handle, err);
        mysql_close(handle);
        return nullptr;
    }
    mysql_set_character_set(handle, charset.c_str());
    auto connection = std::make_unique<MySqlConnection>(handle, params);
    connection->SetReadOnly(params.read_only);
    return connection;
}

}  // namespace silkit::db
