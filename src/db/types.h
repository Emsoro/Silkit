#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace silkit::db {

enum class Engine { Sqlite, MySql, Postgres };

const char* EngineName(Engine engine);
Engine EngineFromName(const std::string& name, bool& ok);

enum class ValueKind {
    Null,
    Int,
    UInt,
    Float,
    Decimal,
    Bool,
    Text,
    Binary,
    Date,
    Time,
    DateTime,
    Timestamp,
    Json,
    Uuid,
    Array,
    Unknown
};

const char* KindName(ValueKind kind);

struct ColumnMeta {
    std::string name;
    std::string db_type;  // 原始类型：varchar(64) / int4 / TEXT
    ValueKind kind = ValueKind::Unknown;
    bool nullable = true;
    bool primary_key = false;
    bool auto_increment = false;
    bool generated = false;
    std::string default_expr;
    std::string comment;
};

struct Cell {
    ValueKind kind = ValueKind::Null;
    bool is_null = true;
    std::string text;         // UTF-8；Binary 为 base64
    std::uint64_t bytes = 0;  // 原始字节数
    bool truncated = false;
};

struct RowBatch {
    std::vector<ColumnMeta> columns;  // 每个结果集的首批会带上列元数据
    std::vector<std::vector<Cell>> rows;
};

/// 返回 false 表示驱动应尽快停止读取（用户取消/达到上限）。
using RowSink = std::function<bool(const RowBatch&)>;

struct Param {
    ValueKind kind = ValueKind::Text;
    bool is_null = false;
    std::string text;  // Binary 为 base64
};

struct ExecOptions {
    std::size_t fetch_size = 500;
    std::size_t max_rows = 0;  // 0 = 不限
    std::uint64_t timeout_ms = 0;
    std::size_t max_cell_bytes = 262144;
};

struct DbError {
    int code = 0;
    std::string sql_state;
    std::string message;
    std::string detail;
    std::string hint;
    int position = -1;  // 语句内字符偏移（1-based），未知为 -1
};

struct ExecResult {
    std::vector<ColumnMeta> columns;
    std::uint64_t rows = 0;
    std::uint64_t affected = 0;
    std::uint64_t last_insert_id = 0;
    std::string info;  // 服务端消息（warning 数等）
    std::uint64_t elapsed_ms = 0;
    bool cancelled = false;
    bool truncated = false;
    DbError error;  // error.code == 0 表示成功
};

}  // namespace silkit::db