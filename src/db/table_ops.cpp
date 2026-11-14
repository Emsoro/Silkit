#include "db/table_ops.h"

#include "db/engine_dialect.h"
#include "db/identifier.h"
#include "db/type_map.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <utility>

namespace silkit::db {
namespace {

struct RowsResult {
    ExecResult result;
    std::vector<std::vector<Cell>> rows;
    bool ok() const { return result.error.code == 0; }
};

RowsResult Run(Connection& connection, const std::string& sql,
               const std::vector<Param>& params = {}) {
    RowsResult out;
    ExecOptions options;
    options.fetch_size = 1000;
    options.max_rows = 0;
    options.max_cell_bytes = 0;
    out.result = connection.Execute(sql, params, options, [&out](const RowBatch& batch) {
        for (const auto& row : batch.rows) out.rows.push_back(row);
        return true;
    });
    return out;
}

std::string CellText(const Cell& cell) { return cell.is_null ? std::string{} : cell.text; }

std::string RowText(const std::vector<Cell>& row, std::size_t index) {
    return index < row.size() ? CellText(row[index]) : std::string{};
}

std::int64_t RowInt(const std::vector<Cell>& row, std::size_t index, std::int64_t fallback = 0) {
    if (index >= row.size() || row[index].is_null) return fallback;
    char* end = nullptr;
    const long long value = std::strtoll(row[index].text.c_str(), &end, 10);
    return end != nullptr && *end == '\0' ? value : fallback;
}

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

std::string Upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return text;
}

bool IsTrue(const std::string& value) {
    const std::string text = Lower(value);
    return text == "t" || text == "true" || text == "1" || text == "yes";
}

bool ContainsNoCase(const std::string& haystack, const char* needle) {
    return Lower(haystack).find(needle) != std::string::npos;
}

std::string Trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) --end;
    return text.substr(begin, end - begin);
}

std::string Placeholder(Engine engine, std::size_t index) {
    return engine == Engine::Postgres ? "$" + std::to_string(index) : "?";
}

void SetError(DbError& error, const std::string& message) {
    error.code = 1;
    error.message = message;
}

std::string ScopeFor(Connection& connection, const TableRef& ref) {
    switch (connection.EngineKind()) {
    case Engine::MySql: return ref.database.empty() ? connection.CurrentDatabase() : ref.database;
    case Engine::Postgres: return ref.schema.empty() ? "public" : ref.schema;
    default: return ref.database.empty() ? "main" : ref.database;
    }
}

std::string QualifiedTable(Engine engine, const std::string& scope, const std::string& name) {
    return QuoteIdentifier(engine, scope) + "." + QuoteIdentifier(engine, name);
}

std::string Pragma(Engine, const std::string& scope, const char* name, const std::string& argument) {
    return "PRAGMA " + QuoteIdentifier(Engine::Sqlite, scope) + "." + name + "(" +
           QuoteStringLiteral(argument) + ")";
}

Param TextParam(const TableValue& value) {
    Param param;
    param.kind = ValueKind::Text;
    param.is_null = value.is_null;
    param.text = value.is_null ? std::string{} : value.text;
    return param;
}

Param ParamForColumn(const TableColumn& column, const TableValue& value) {
    Param param = TextParam(value);
    if (!value.is_null && column.meta.kind == ValueKind::Binary) param.kind = ValueKind::Binary;
    return param;
}

Param ParamForColumn(const TableColumn& column, const std::string& value) {
    TableValue text;
    text.text = value;
    return ParamForColumn(column, text);
}

void LoadSqliteColumns(Connection& connection, const TableLayout& layout,
                       std::vector<TableColumn>& columns, std::vector<std::string>& primary_keys,
                       DbError& error) {
    auto rows = Run(connection, Pragma(Engine::Sqlite, layout.scope, "table_xinfo", layout.ref.name));
    if (!rows.ok()) {
        error = rows.result.error;
        return;
    }

    std::vector<std::pair<std::int64_t, std::string>> pk_order;
    for (const auto& row : rows.rows) {
        const std::int64_t hidden = RowInt(row, 6, 0);
        if (hidden == 1) continue;  // 虚拟表隐藏列
        TableColumn column;
        column.meta.name = RowText(row, 1);
        column.meta.db_type = RowText(row, 2);
        column.meta.kind = KindFromDeclType(column.meta.db_type);
        column.meta.nullable = RowInt(row, 3, 0) == 0;
        column.meta.default_expr = RowText(row, 4);
        const std::int64_t pk = RowInt(row, 5, 0);
        column.meta.primary_key = pk > 0;
        column.meta.generated = hidden == 2 || hidden == 3;
        column.meta.auto_increment =
            column.meta.primary_key && column.meta.kind == ValueKind::Int &&
            Upper(Trim(column.meta.db_type)) == "INTEGER";
        if (pk > 0) pk_order.emplace_back(pk, column.meta.name);
        columns.push_back(std::move(column));
    }

    std::sort(pk_order.begin(), pk_order.end(),
              [](const auto& left, const auto& right) { return left.first < right.first; });
    for (const auto& item : pk_order) primary_keys.push_back(item.second);
}

void LoadEngineColumns(Connection& connection, const TableLayout& layout,
                       std::vector<TableColumn>& columns, std::vector<std::string>& primary_keys,
                       DbError& error) {
    auto pk_rows = Run(connection, ListPrimaryKeysSql(layout.engine, layout.scope, layout.ref.name));
    if (pk_rows.ok()) {
        for (const auto& row : pk_rows.rows) primary_keys.push_back(RowText(row, 0));
    }

    auto rows = Run(connection, ListColumnsSql(layout.engine, layout.scope, layout.ref.name));
    if (!rows.ok()) {
        error = rows.result.error;
        return;
    }

    for (const auto& row : rows.rows) {
        TableColumn column;
        column.meta.name = RowText(row, 0);
        column.meta.db_type = RowText(row, 1);
        column.meta.kind = KindFromDeclType(column.meta.db_type);
        if (layout.engine == Engine::MySql) {
            column.meta.nullable = RowText(row, 2) != "NO";
            column.meta.default_expr = RowText(row, 3);
            const std::string extra = RowText(row, 4);
            column.meta.auto_increment = ContainsNoCase(extra, "auto_increment");
            column.meta.generated = ContainsNoCase(extra, "generated") ||
                                    ContainsNoCase(extra, "virtual") ||
                                    ContainsNoCase(extra, "stored");
            column.meta.comment = RowText(row, 6);
        } else {
            column.meta.nullable = !IsTrue(RowText(row, 2));
            column.meta.default_expr = RowText(row, 3);
            column.meta.auto_increment = IsTrue(RowText(row, 4)) ||
                                         ContainsNoCase(column.meta.default_expr, "nextval(");
            column.meta.generated = IsTrue(RowText(row, 5));
            column.meta.comment = RowText(row, 6);
        }
        column.meta.primary_key =
            std::find(primary_keys.begin(), primary_keys.end(), column.meta.name) !=
            primary_keys.end();
        columns.push_back(std::move(column));
    }
}

std::vector<std::string> SqliteUniqueColumns(Connection& connection, const TableLayout& layout,
                                             std::string& index_name) {
    auto indexes = Run(connection, Pragma(Engine::Sqlite, layout.scope, "index_list", layout.ref.name));
    if (!indexes.ok()) return {};

    for (const auto& row : indexes.rows) {
        if (RowInt(row, 2, 0) == 0) continue;   // unique
        if (RowInt(row, 4, 0) != 0) continue;   // partial
        const std::string name = RowText(row, 1);
        auto info = Run(connection, Pragma(Engine::Sqlite, layout.scope, "index_info", name));
        if (!info.ok() || info.rows.empty()) continue;
        std::vector<std::string> columns;
        for (const auto& info_row : info.rows) columns.push_back(RowText(info_row, 2));
        if (columns.empty()) continue;
        index_name = name;
        return columns;
    }
    return {};
}

const TableColumn* FindColumn(const std::vector<TableColumn>& columns, const std::string& name) {
    for (const auto& column : columns) {
        if (column.meta.name == name) return &column;
    }
    const std::string lowered = Lower(name);
    for (const auto& column : columns) {
        if (Lower(column.meta.name) == lowered) return &column;
    }
    return nullptr;
}

void DetectIdentity(Connection& connection, TableLayout& layout,
                    const std::vector<std::string>& primary_keys, DbError& error) {
    if (!primary_keys.empty()) {
        layout.identity.type = "pk";
        layout.identity.columns = primary_keys;
        return;
    }

    // 唯一索引：所有列都必须 NOT NULL，且索引不是部分/表达式索引。
    std::map<std::string, std::vector<std::string>> unique_indexes;
    if (layout.engine == Engine::Sqlite) {
        std::string index_name;
        const auto columns = SqliteUniqueColumns(connection, layout, index_name);
        if (!columns.empty()) unique_indexes[index_name] = columns;
    } else {
        auto rows = Run(connection, ListUniqueIndexColumnsSql(layout.engine, layout.scope,
                                                             layout.ref.name));
        if (rows.ok()) {
            for (const auto& row : rows.rows) {
                unique_indexes[RowText(row, 0)].push_back(RowText(row, 1));
            }
        }
    }

    for (const auto& item : unique_indexes) {
        if (item.second.empty()) continue;
        bool usable = true;
        for (const auto& name : item.second) {
            const TableColumn* column = FindColumn(layout.columns, name);
            if (column == nullptr || column->meta.nullable) {
                usable = false;
                break;
            }
        }
        if (!usable) continue;
        layout.identity.type = "unique";
        layout.identity.columns = item.second;
        layout.identity.index_name = item.first;
        return;
    }

    if (layout.engine == Engine::Sqlite && layout.ref.kind != "view") {
        auto probe = Run(connection, "SELECT rowid FROM " + layout.qualified + " LIMIT 1");
        if (probe.ok()) {
            layout.identity.type = "rowid";
            layout.identity.columns = {"rowid"};
        }
    }
    (void)error;
}

std::string CastToText(Engine engine, const std::string& column) {
    if (engine == Engine::MySql) return "CAST(" + column + " AS CHAR)";
    return "CAST(" + column + " AS TEXT)";
}

bool AppendWhere(Connection& connection, const TableLayout& layout, const RowsQuery& query,
                 std::string& sql, std::vector<Param>& params, std::string& error) {
    (void)connection;
    std::vector<std::string> clauses;
    const Engine engine = layout.engine;

    if (!query.search.empty()) {
        std::vector<std::string> search_clauses;
        for (const auto& column : layout.columns) {
            if (column.meta.kind == ValueKind::Binary) continue;
            const std::string quoted = QuoteIdentifier(engine, column.meta.name);
            TableColumn text_column = column;
            text_column.meta.kind = ValueKind::Text;
            const std::string like =
                engine == Engine::Postgres
                    ? CastToText(engine, quoted) + " ILIKE " + Placeholder(engine, params.size() + 1)
                    : CastToText(engine, quoted) + " LIKE " + Placeholder(engine, params.size() + 1);
            search_clauses.push_back(like);
            params.push_back(ParamForColumn(text_column, "%" + query.search + "%"));
        }
        if (!search_clauses.empty()) {
            std::string combined = "(";
            for (std::size_t i = 0; i < search_clauses.size(); ++i) {
                if (i != 0) combined += " OR ";
                combined += search_clauses[i];
            }
            combined += ")";
            clauses.push_back(std::move(combined));
        }
    }

    for (const auto& filter : query.filters) {
        const TableColumn* column = FindColumn(layout.columns, filter.column);
        if (column == nullptr) {
            error = "筛选列不存在: " + filter.column;
            return false;
        }
        const std::string quoted = QuoteIdentifier(engine, column->meta.name);
        const std::string op = Upper(Trim(filter.op.empty() ? std::string{"="} : filter.op));

        if (op == "IS NULL") {
            clauses.push_back(quoted + " IS NULL");
            continue;
        }
        if (op == "IS NOT NULL") {
            clauses.push_back(quoted + " IS NOT NULL");
            continue;
        }
        if (op != "=" && op != "==" && op != "!=" && op != "<>" && op != ">" && op != ">=" &&
            op != "<" && op != "<=" && op != "LIKE" && op != "NOT LIKE" && op != "IN" &&
            op != "NOT IN" && op != "BETWEEN") {
            error = "不支持的筛选操作符: " + filter.op;
            return false;
        }

        if (op == "IN" || op == "NOT IN") {
            std::vector<std::string> values = filter.values;
            if (values.empty()) {
                std::string raw = filter.value;
                std::size_t start = 0;
                for (;;) {
                    const std::size_t comma = raw.find(',', start);
                    const std::string item = Trim(raw.substr(start, comma == std::string::npos
                                                                      ? std::string::npos
                                                                      : comma - start));
                    if (!item.empty()) values.push_back(item);
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
            }
            if (values.empty()) {
                error = "IN 筛选缺少值: " + filter.column;
                return false;
            }
            std::string in = quoted + (op == "IN" ? " IN (" : " NOT IN (");
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (i != 0) in += ", ";
                in += Placeholder(engine, params.size() + 1);
                params.push_back(ParamForColumn(*column, values[i]));
            }
            in += ")";
            clauses.push_back(std::move(in));
            continue;
        }

        if (op == "BETWEEN") {
            if (filter.value_is_null || filter.value2.empty()) {
                error = "BETWEEN 筛选需要两个值: " + filter.column;
                return false;
            }
            clauses.push_back(quoted + " BETWEEN " + Placeholder(engine, params.size() + 1) +
                              " AND " + Placeholder(engine, params.size() + 2));
            TableValue first;
            first.text = filter.value;
            TableValue second;
            second.text = filter.value2;
            params.push_back(ParamForColumn(*column, first));
            params.push_back(ParamForColumn(*column, second));
            continue;
        }

        if (filter.value_is_null) {
            error = "筛选值不能为空（IS NULL 请使用对应操作符）: " + filter.column;
            return false;
        }
        const std::string sql_op = op == "==" ? "=" : op;
        clauses.push_back(quoted + " " + sql_op + " " + Placeholder(engine, params.size() + 1));
        params.push_back(ParamForColumn(*column, filter.value));
    }

    if (clauses.empty()) return true;
    sql += " WHERE ";
    for (std::size_t i = 0; i < clauses.size(); ++i) {
        if (i != 0) sql += " AND ";
        sql += clauses[i];
    }
    return true;
}

}  // namespace

const TableColumn* FindColumn(const TableLayout& layout, const std::string& name) {
    return FindColumn(layout.columns, name);
}

bool HasRowIdSelect(const TableLayout& layout) { return layout.identity.type == "rowid"; }

bool LoadTableLayout(Connection& connection, const TableRef& ref, bool session_read_only,
                     TableLayout& layout, DbError& error) {
    layout = TableLayout{};
    layout.ref = ref;
    layout.ref.kind = ref.kind == "view" ? "view" : "table";
    layout.engine = connection.EngineKind();
    if (!IsSafeIdentifier(ref.name)) {
        SetError(error, "表名不合法");
        return false;
    }
    layout.scope = ScopeFor(connection, ref);
    layout.qualified = QualifiedTable(layout.engine, layout.scope, ref.name);

    std::vector<std::string> primary_keys;
    if (layout.engine == Engine::Sqlite) {
        LoadSqliteColumns(connection, layout, layout.columns, primary_keys, error);
    } else {
        LoadEngineColumns(connection, layout, layout.columns, primary_keys, error);
    }
    if (error.code != 0) return false;
    if (layout.columns.empty()) {
        SetError(error, "表不存在或没有列: " + ref.name);
        return false;
    }

    DetectIdentity(connection, layout, primary_keys, error);
    if (error.code != 0) return false;

    if (layout.ref.kind == "view") {
        layout.editable = false;
        layout.read_only_reason = "视图为只读";
    } else if (session_read_only || connection.IsReadOnly()) {
        layout.editable = false;
        layout.read_only_reason = "连接为只读模式，不能编辑数据";
    } else if (layout.identity.type == "none") {
        layout.editable = false;
        layout.read_only_reason = "缺少主键、唯一非空索引或 rowid，无法安全定位行";
    } else {
        layout.editable = true;
    }
    return true;
}

bool BuildRowsQuery(Connection& connection, const TableLayout& layout, const RowsQuery& query,
                    BuiltQuery& out, std::string& error) {
    const Engine engine = layout.engine;
    const int page = std::max(0, query.page);
    const int page_size = std::clamp(query.page_size, 1, 2000);

    std::string sql = "SELECT ";
    if (HasRowIdSelect(layout)) sql += "rowid, ";
    for (std::size_t i = 0; i < layout.columns.size(); ++i) {
        if (i != 0) sql += ", ";
        sql += QuoteIdentifier(engine, layout.columns[i].meta.name);
    }
    sql += " FROM " + layout.qualified;

    std::vector<Param> params;
    if (!AppendWhere(connection, layout, query, sql, params, error)) return false;

    if (!query.order.column.empty()) {
        const TableColumn* column = FindColumn(layout.columns, query.order.column);
        if (column != nullptr) {
            sql += " ORDER BY " + QuoteIdentifier(engine, column->meta.name);
            if (query.order.desc) sql += " DESC";
        }
    }

    sql += " LIMIT " + std::to_string(page_size + 1) + " OFFSET " +
           std::to_string(static_cast<std::int64_t>(page) * page_size);
    out.sql = std::move(sql);
    out.params = std::move(params);
    return true;
}

bool BuildCountQuery(Connection& connection, const TableLayout& layout, const RowsQuery& query,
                     BuiltQuery& out, std::string& error) {
    std::string sql = "SELECT COUNT(*) FROM " + layout.qualified;
    std::vector<Param> params;
    if (!AppendWhere(connection, layout, query, sql, params, error)) return false;
    out.sql = std::move(sql);
    out.params = std::move(params);
    return true;
}

bool BuildIdentityWhere(Connection& connection, const TableLayout& layout,
                        const std::map<std::string, TableValue>& keys, std::string& clause,
                        std::vector<Param>& params, std::string& error) {
    (void)connection;
    if (layout.identity.type == "none" || layout.identity.columns.empty()) {
        error = "该表没有可用于定位行的主键/唯一索引/rowid";
        return false;
    }

    const Engine engine = layout.engine;
    std::string sql;
    for (std::size_t i = 0; i < layout.identity.columns.size(); ++i) {
        const std::string& name = layout.identity.columns[i];
        auto found = keys.find(name);
        if (found == keys.end()) {
            // 兼容大小写不同的键名。
            for (const auto& item : keys) {
                if (Lower(item.first) == Lower(name)) {
                    found = keys.find(item.first);
                    break;
                }
            }
        }
        if (found == keys.end() || found->second.is_null) {
            error = "缺少行标识列的值: " + name;
            return false;
        }
        if (i != 0) sql += " AND ";
        sql += ((layout.identity.type == "rowid" ? std::string("rowid")
                                                  : QuoteIdentifier(engine, name))) +
               " = " + Placeholder(engine, params.size() + 1);
        if (layout.identity.type == "rowid") {
            params.push_back(TextParam(found->second));
        } else {
            const TableColumn* column = FindColumn(layout.columns, name);
            if (column == nullptr) {
                error = "行标识列不存在: " + name;
                return false;
            }
            params.push_back(ParamForColumn(*column, found->second));
        }
    }
    clause = std::move(sql);
    return true;
}

bool BuildChanges(Connection& connection, const TableLayout& layout, const TableChanges& changes,
                  std::vector<PreparedChange>& out, std::string& error) {
    out.clear();
    if (!layout.editable) {
        error = layout.read_only_reason.empty() ? "该表只读" : layout.read_only_reason;
        return false;
    }
    const Engine engine = layout.engine;

    for (const auto& insert : changes.inserts) {
        PreparedChange prepared;
        prepared.kind = "insert";
        prepared.client_index = insert.client_index;
        std::string columns;
        std::string values;
        for (const auto& item : insert.values) {
            const TableColumn* column = FindColumn(layout.columns, item.first);
            if (column == nullptr) {
                error = "插入值包含未知列: " + item.first;
                return false;
            }
            if (column->meta.generated) {
                error = "生成列不能写入: " + column->meta.name;
                return false;
            }
            if (!columns.empty()) {
                columns += ", ";
                values += ", ";
            }
            columns += QuoteIdentifier(engine, column->meta.name);
            values += Placeholder(engine, prepared.params.size() + 1);
            prepared.params.push_back(ParamForColumn(*column, item.second));
        }
        if (columns.empty()) {
            prepared.sql = engine == Engine::MySql
                               ? "INSERT INTO " + layout.qualified + " () VALUES ()"
                               : "INSERT INTO " + layout.qualified + " DEFAULT VALUES";
        } else {
            prepared.sql = "INSERT INTO " + layout.qualified + " (" + columns + ") VALUES (" +
                           values + ")";
        }
        out.push_back(std::move(prepared));
    }

    for (const auto& update : changes.updates) {
        std::string set;
        PreparedChange prepared;
        prepared.kind = "update";
        prepared.client_index = update.client_index;
        for (const auto& item : update.changes) {
            const TableColumn* column = FindColumn(layout.columns, item.first);
            if (column == nullptr) {
                error = "更新值包含未知列: " + item.first;
                return false;
            }
            if (column->meta.generated) {
                error = "生成列不能修改: " + column->meta.name;
                return false;
            }
            if (!set.empty()) set += ", ";
            set += QuoteIdentifier(engine, column->meta.name) + " = " +
                   Placeholder(engine, prepared.params.size() + 1);
            prepared.params.push_back(ParamForColumn(*column, item.second));
        }
        if (set.empty()) {
            error = "更新没有需要写入的列";
            return false;
        }
        std::string where;
        if (!BuildIdentityWhere(connection, layout, update.keys, where, prepared.params, error)) {
            return false;
        }
        prepared.sql = "UPDATE " + layout.qualified + " SET " + set + " WHERE " + where;
        out.push_back(std::move(prepared));
    }

    for (const auto& remove : changes.deletes) {
        PreparedChange prepared;
        prepared.kind = "delete";
        prepared.client_index = remove.client_index;
        std::string where;
        if (!BuildIdentityWhere(connection, layout, remove.keys, where, prepared.params, error)) {
            return false;
        }
        prepared.sql = "DELETE FROM " + layout.qualified + " WHERE " + where;
        out.push_back(std::move(prepared));
    }
    return true;
}

bool EstimateTotalRows(Connection& connection, const TableLayout& layout, const RowsQuery& query,
                       std::int64_t& out, std::string& error) {
    out = -1;
    if (!query.filters.empty() || !query.search.empty()) return true;

    if (layout.engine == Engine::Sqlite) {
        auto rows = Run(connection, "SELECT COUNT(*) FROM " + layout.qualified);
        if (!rows.ok() || rows.rows.empty()) {
            error = rows.result.error.message;
            return false;
        }
        out = RowInt(rows.rows.front(), 0, -1);
        return true;
    }

    if (layout.engine == Engine::MySql) {
        auto rows = Run(connection,
                        "SELECT TABLE_ROWS FROM information_schema.TABLES WHERE TABLE_SCHEMA = " +
                            QuoteStringLiteral(layout.scope) + " AND TABLE_NAME = " +
                            QuoteStringLiteral(layout.ref.name));
        if (!rows.ok() || rows.rows.empty()) return true;
        out = RowInt(rows.rows.front(), 0, -1);
        return true;
    }

    auto rows = Run(connection,
                    "SELECT c.reltuples::bigint FROM pg_class c"
                    " JOIN pg_namespace n ON n.oid = c.relnamespace"
                    " WHERE n.nspname = " + QuoteStringLiteral(layout.scope) +
                        " AND c.relname = " + QuoteStringLiteral(layout.ref.name));
    if (rows.ok() && !rows.rows.empty()) out = RowInt(rows.rows.front(), 0, -1);
    return true;
}

}  // namespace silkit::db
