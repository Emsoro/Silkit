#include "db/drivers/postgres_driver.h"

#include "db/engine_dialect.h"
#include "db/sql_util.h"
#include "db/type_map.h"
#include "util/base64.h"

#include <libpq-fe.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace silkit::db {
namespace {

const char* kCancelledSqlState = "57014";

std::string CutUtf8(const std::string& text, std::size_t limit) {
    if (text.size() <= limit) return text;
    std::size_t end = limit;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
    return text.substr(0, end);
}

const char* PgTypeName(Oid oid) {
    switch (oid) {
    case 16: return "bool";
    case 17: return "bytea";
    case 20: return "int8";
    case 21: return "int2";
    case 23: return "int4";
    case 25: return "text";
    case 700: return "float4";
    case 701: return "float8";
    case 1082: return "date";
    case 1083: return "time";
    case 1114: return "timestamp";
    case 1184: return "timestamptz";
    case 1186: return "interval";
    case 114: return "json";
    case 1700: return "numeric";
    case 2950: return "uuid";
    case 3802: return "jsonb";
    default: return nullptr;
    }
}

std::string TypeLabel(Oid oid) {
    const char* name = PgTypeName(oid);
    if (name) return name;
    if (KindFromPgOid(oid) == ValueKind::Array) return "anyarray";
    return "oid:" + std::to_string(static_cast<unsigned int>(oid));
}

std::string HexBytes(const char* value, int length) {
    if (length < 2 || value[0] != '\\' || value[1] != 'x') return {};
    auto nibble = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    std::string bytes;
    bytes.reserve(static_cast<std::size_t>(length / 2));
    for (int i = 2; i + 1 < length; i += 2) {
        const int hi = nibble(value[i]);
        const int lo = nibble(value[i + 1]);
        if (hi < 0 || lo < 0) return {};
        bytes.push_back(static_cast<char>((hi << 4) | lo));
    }
    return bytes;
}

/// bytea 的 hex 输出转 base64；其余类型保留服务端文本。
Cell MakeCell(PGresult* res, int row, int column, Oid oid, ValueKind kind,
              std::size_t max_bytes) {
    Cell cell;
    if (PQgetisnull(res, row, column)) return cell;
    cell.is_null = false;
    cell.kind = kind;
    const char* value = PQgetvalue(res, row, column);
    const int length = PQgetlength(res, row, column);
    cell.bytes = static_cast<std::uint64_t>(length);

    if (oid == 17) {  // bytea
        const std::string bytes = HexBytes(value, length);
        std::string encoded = util::Base64Encode(bytes.data(), bytes.size());
        cell.bytes = bytes.size();
        if (max_bytes > 0 && encoded.size() > max_bytes) {
            encoded = CutUtf8(encoded, max_bytes);
            cell.truncated = true;
        }
        cell.text = std::move(encoded);
        return cell;
    }

    std::string text(value, static_cast<std::size_t>(length));
    if (max_bytes > 0 && text.size() > max_bytes) {
        text = CutUtf8(text, max_bytes);
        cell.truncated = true;
    }
    cell.text = std::move(text);
    return cell;
}

std::string Base64ToByteaHex(const std::string& encoded, std::string& storage) {
    std::string bytes;
    if (!util::Base64Decode(encoded, bytes)) return {};
    static const char* kHex = "0123456789abcdef";
    storage = "\\x";
    storage.reserve(2 + bytes.size() * 2);
    for (unsigned char ch : bytes) {
        storage.push_back(kHex[ch >> 4]);
        storage.push_back(kHex[ch & 0x0F]);
    }
    return storage;
}

void FillConnError(PGconn* conn, DbError& error) {
    error.code = -1;
    const char* message = PQerrorMessage(conn);
    error.message = message ? message : "PostgreSQL 连接错误";
    DecorateError(Engine::Postgres, error);
    if (error.hint.empty()) {
        error.hint = "无法连接到 PostgreSQL 服务器，请检查主机、端口、数据库名、用户与 SSL 设置。";
    }
}

void FillResultError(PGresult* res, DbError& error) {
    error.code = -1;
    const auto field = [res](int code) -> std::string {
        const char* value = PQresultErrorField(res, code);
        return value ? value : "";
    };
    error.sql_state = field(PG_DIAG_SQLSTATE);
    error.message = field(PG_DIAG_MESSAGE_PRIMARY);
    if (error.message.empty()) error.message = "PostgreSQL 语句执行失败";
    error.detail = field(PG_DIAG_MESSAGE_DETAIL);
    const std::string server_hint = field(PG_DIAG_MESSAGE_HINT);
    const std::string position = field(PG_DIAG_STATEMENT_POSITION);
    if (!position.empty()) {
        try { error.position = std::stoi(position); } catch (...) { error.position = -1; }
    }
    DbError decorated = error;
    DecorateError(Engine::Postgres, decorated);
    if (!server_hint.empty()) {
        error.hint = server_hint;
        if (!decorated.hint.empty() && decorated.hint != server_hint) {
            error.hint += " · " + decorated.hint;
        }
    } else {
        error.hint = decorated.hint;
    }
}

class PgConnection final : public Connection {
public:
    PgConnection(PGconn* conn, ConnectParams params)
        : conn_(conn), params_(std::move(params)) {
        cancel_ = PQgetCancel(conn_);
    }

    ~PgConnection() override {
        if (cancel_) PQfreeCancel(cancel_);
        if (conn_) PQfinish(conn_);
    }

    Engine EngineKind() const override { return Engine::Postgres; }

    Capabilities Caps() const override {
        Capabilities caps;
        caps.databases = true;
        caps.schemas = true;
        caps.routines = true;
        caps.sequences = true;
        caps.transactional_ddl = true;
        caps.user_management = true;
        caps.table_comment = true;
        caps.events = false;
        return caps;
    }

    bool Ping(DbError& err) override {
        if (PQstatus(conn_) != CONNECTION_OK) {
            FillConnError(conn_, err);
            return false;
        }
        PGresult* res = PQexec(conn_, "SELECT 1");
        const bool ok = res && PQresultStatus(res) == PGRES_TUPLES_OK;
        if (!ok) FillResultError(res, err);
        if (res) PQclear(res);
        return ok;
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

        std::vector<std::string> binary_storage(params.size());
        std::vector<std::string> texts(params.size());
        std::vector<const char*> values(params.size(), nullptr);
        std::vector<int> lengths(params.size(), 0);
        std::vector<int> formats(params.size(), 0);
        for (std::size_t i = 0; i < params.size(); ++i) {
            if (params[i].is_null) continue;
            if (params[i].kind == ValueKind::Binary) {
                texts[i] = Base64ToByteaHex(params[i].text, binary_storage[i]);
            } else {
                texts[i] = params[i].text;
            }
            values[i] = texts[i].c_str();
            lengths[i] = static_cast<int>(texts[i].size());
            formats[i] = 0;  // 文本格式
        }

        const int sent = PQsendQueryParams(
            conn_, sql.c_str(), static_cast<int>(params.size()), nullptr,
            params.empty() ? nullptr : values.data(),
            params.empty() ? nullptr : lengths.data(),
            params.empty() ? nullptr : formats.data(), 0);
        if (sent != 1) {
            FillConnError(conn_, result.error);
            result.elapsed_ms = Elapsed(started);
            return result;
        }
        if (PQsetSingleRowMode(conn_) != 1) {
            FillConnError(conn_, result.error);
            DrainResults();
            result.elapsed_ms = Elapsed(started);
            return result;
        }

        RowBatch batch;
        bool have_columns = false;
        bool stop = false;
        std::size_t emitted = 0;
        std::vector<Oid> oids;
        std::vector<ValueKind> kinds;

        PGresult* res = nullptr;
        while ((res = PQgetResult(conn_)) != nullptr) {
            const ExecStatusType status = PQresultStatus(res);
            if (status == PGRES_SINGLE_TUPLE) {
                const int columns = PQnfields(res);
                if (!have_columns) {
                    have_columns = true;
                    oids.resize(columns);
                    kinds.resize(columns);
                    for (int c = 0; c < columns; ++c) {
                        const Oid oid = PQftype(res, c);
                        ColumnMeta meta;
                        meta.name = PQfname(res, c) ? PQfname(res, c) : "";
                        meta.db_type = TypeLabel(oid);
                        meta.kind = KindFromPgOid(oid);
                        meta.nullable = true;
                        result.columns.push_back(std::move(meta));
                        oids[c] = oid;
                        kinds[c] = result.columns.back().kind;
                    }
                    batch.columns = result.columns;
                }
                std::vector<Cell> cells(static_cast<std::size_t>(columns));
                for (int c = 0; c < columns; ++c) {
                    cells[c] = MakeCell(res, 0, c, oids[static_cast<std::size_t>(c)],
                                        kinds[static_cast<std::size_t>(c)], opt.max_cell_bytes);
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
            } else if (status == PGRES_COMMAND_OK) {
                const char* tuples = PQcmdTuples(res);
                if (tuples && tuples[0]) {
                    try { result.affected = std::stoull(tuples); } catch (...) {}
                }
                const char* status_text = PQcmdStatus(res);
                result.info = status_text ? status_text : "";
            } else if (status == PGRES_FATAL_ERROR) {
                FillResultError(res, result.error);
                result.cancelled = result.error.sql_state == kCancelledSqlState;
                stop = true;
            }
            PQclear(res);
            if (stop) break;
        }

        if (!stop && !batch.rows.empty() && sink) {
            result.rows += batch.rows.size();
            sink(batch);
        } else if (!stop && result.rows == 0 && !have_columns && sink) {
            sink(batch);  // 无结果集语句无需补发列元数据，保持与 SQLite 行为一致由上层判断
        }
        if (stop) DrainResults();
        result.elapsed_ms = Elapsed(started);
        return result;
    }

    void Cancel() override {
        std::lock_guard<std::mutex> lock(cancel_mutex_);
        if (!cancel_) return;
        char buffer[256] = {};
        PQcancel(cancel_, buffer, static_cast<int>(sizeof(buffer)));
    }

    void SetReadOnly(bool read_only) override { read_only_ = read_only; }
    bool IsReadOnly() const override { return read_only_; }

    std::string ServerVersion() const override {
        const int version = PQserverVersion(conn_);
        const int major = version / 10000;
        const int minor = (version / 100) % 100;
        const int patch = version % 100;
        return std::to_string(major) + "." + std::to_string(minor) +
               (patch > 0 ? "." + std::to_string(patch) : "");
    }

    std::string CurrentDatabase() const override {
        const char* db = PQdb(conn_);
        return db ? db : params_.database;
    }

    std::uint64_t ThreadId() const override {
        return static_cast<std::uint64_t>(std::max(0, PQbackendPID(conn_)));
    }

    std::string Placeholder(std::size_t index) const override {
        return "$" + std::to_string(index);
    }

private:
    static std::uint64_t Elapsed(const std::chrono::steady_clock::time_point& started) {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count());
    }

    void DrainResults() {
        PGresult* pending = nullptr;
        while ((pending = PQgetResult(conn_)) != nullptr) PQclear(pending);
    }

    PGconn* conn_;
    ConnectParams params_;
    PGcancel* cancel_ = nullptr;
    bool read_only_ = false;
    std::mutex cancel_mutex_;
};

std::string BuildOptions(const ConnectParams& params) {
    std::string options = "-c bytea_output=hex";
    if (!params.schema.empty()) {
        options += " -c search_path=" + params.schema;
    }
    return options;
}

}  // namespace

std::unique_ptr<Connection> PostgresDriver::Open(const ConnectParams& params, DbError& err) {
    std::vector<std::string> values;
    std::vector<const char*> keywords;
    std::vector<const char*> value_ptrs;
    const auto add = [&](const char* key, std::string value) {
        keywords.push_back(key);
        values.push_back(std::move(value));
    };
    add("host", params.host.empty() ? "127.0.0.1" : params.host);
    add("port", params.port.empty() ? DefaultPort(Engine::Postgres) : params.port);
    add("dbname", params.database.empty() ? DefaultDatabase(Engine::Postgres) : params.database);
    add("user", params.user.empty() ? "postgres" : params.user);
    add("password", params.password);
    add("connect_timeout", std::to_string(std::max(1, params.connect_timeout_ms / 1000)));
    add("application_name", params.application_name.empty() ? "Silkit" : params.application_name);
    add("sslmode", params.ssl_mode.empty() ? "prefer" : params.ssl_mode);
    if (!params.ssl_ca.empty()) add("sslrootcert", params.ssl_ca);
    if (!params.ssl_cert.empty()) add("sslcert", params.ssl_cert);
    if (!params.ssl_key.empty()) add("sslkey", params.ssl_key);
    add("options", BuildOptions(params));
    keywords.push_back(nullptr);
    for (const auto& value : values) value_ptrs.push_back(value.c_str());
    value_ptrs.push_back(nullptr);

    PGconn* conn = PQconnectdbParams(keywords.data(), value_ptrs.data(), 0);
    if (!conn) {
        err.code = -1;
        err.message = "无法初始化 libpq 连接";
        return nullptr;
    }
    if (PQstatus(conn) != CONNECTION_OK) {
        FillConnError(conn, err);
        PQfinish(conn);
        return nullptr;
    }
    auto connection = std::make_unique<PgConnection>(conn, params);
    if (params.read_only) {
        PGresult* res = PQexec(conn, "SET SESSION CHARACTERISTICS AS TRANSACTION READ ONLY");
        if (res) PQclear(res);
        connection->SetReadOnly(true);
    }
    if (!params.init_sql.empty()) {
        PGresult* res = PQexec(conn, params.init_sql.c_str());
        if (res) {
            if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK) {
                FillResultError(res, err);
                PQclear(res);
                return nullptr;
            }
            PQclear(res);
        }
    }
    return connection;
}

}  // namespace silkit::db
