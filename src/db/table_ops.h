#pragma once

#include "db/driver.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace silkit::db {

/// 表数据编辑的纯 DB 逻辑：表结构/行标识探测、分页 SQL、筛选 SQL、CRUD 语句生成。
/// IPC 层只负责 JSON 编解码与事务，便于三引擎复用同一套生成器。
struct TableRef {
    std::string kind = "table";  // table | view
    std::string name;
    std::string database;
    std::string schema;
};

struct TableColumn {
    ColumnMeta meta;
};

struct TableIdentity {
    // pk | unique | rowid | none
    std::string type = "none";
    std::vector<std::string> columns;
    std::string index_name;
};

struct TableLayout {
    TableRef ref;
    Engine engine = Engine::Sqlite;
    std::string scope;      // MySQL 数据库 / PG schema / SQLite 附加库
    std::string qualified;  // 已按引擎引用、可直接拼进 SQL 的表名
    std::vector<TableColumn> columns;
    TableIdentity identity;
    bool editable = false;
    std::string read_only_reason;
};

struct TableFilter {
    std::string column;
    std::string op = "=";
    bool value_is_null = false;
    std::string value;
    std::string value2;
    std::vector<std::string> values;  // IN / NOT IN（优先于 value 的分隔解析）
};

struct TableOrder {
    std::string column;
    bool desc = false;
};

struct RowsQuery {
    int page = 0;
    int page_size = 500;
    TableOrder order;
    std::vector<TableFilter> filters;
    std::string search;
};

struct TableValue {
    bool is_null = false;
    std::string text;
};

struct InsertSpec {
    std::map<std::string, TableValue> values;
    std::size_t client_index = 0;
};

struct UpdateSpec {
    std::map<std::string, TableValue> keys;
    std::map<std::string, TableValue> changes;
    std::size_t client_index = 0;
};

struct DeleteSpec {
    std::map<std::string, TableValue> keys;
    std::size_t client_index = 0;
};

struct TableChanges {
    std::vector<InsertSpec> inserts;
    std::vector<UpdateSpec> updates;
    std::vector<DeleteSpec> deletes;
};

struct PreparedChange {
    std::string kind;  // insert | update | delete
    std::size_t client_index = 0;
    std::string sql;
    std::vector<Param> params;
};

struct BuiltQuery {
    std::string sql;
    std::vector<Param> params;
};

/// 读取列、主键/唯一索引/rowid，生成 table.rows 所需的布局与只读原因。
bool LoadTableLayout(Connection& connection, const TableRef& ref, bool session_read_only,
                     TableLayout& layout, DbError& error);

/// 分页/排序/筛选/搜索查询；page_size 会在内部限制到 1..2000。
bool BuildRowsQuery(Connection& connection, const TableLayout& layout, const RowsQuery& query,
                    BuiltQuery& out, std::string& error);

bool BuildCountQuery(Connection& connection, const TableLayout& layout, const RowsQuery& query,
                     BuiltQuery& out, std::string& error);

/// 生成 CRUD 语句；values/keys 均使用列名，参数值由本函数转成 Param。
bool BuildChanges(Connection& connection, const TableLayout& layout, const TableChanges& changes,
                  std::vector<PreparedChange>& out, std::string& error);

/// 单行 WHERE（身份列全部相等）；rowid 身份使用 "rowid" 作为键名。
bool BuildIdentityWhere(Connection& connection, const TableLayout& layout,
                        const std::map<std::string, TableValue>& keys, std::string& clause,
                        std::vector<Param>& params, std::string& error);

/// 仅无筛选/搜索时用目录估算；失败返回 false，out 保持 -1。SQLite 用 COUNT(*)。
bool EstimateTotalRows(Connection& connection, const TableLayout& layout, const RowsQuery& query,
                       std::int64_t& out, std::string& error);

/// SELECT 列表是否带前置 rowid（供 IPC 层拆分显示列与 rowKeys）。
bool HasRowIdSelect(const TableLayout& layout);

const TableColumn* FindColumn(const TableLayout& layout, const std::string& name);

}  // namespace silkit::db
