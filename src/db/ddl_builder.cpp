#include "db/ddl_builder.h"

#include "db/engine_dialect.h"
#include "db/identifier.h"
#include "db/table_ops.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

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
        out.rows.insert(out.rows.end(), batch.rows.begin(), batch.rows.end());
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

bool IsTrue(const std::string& value) {
    std::string text = value;
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text == "t" || text == "true" || text == "1" || text == "yes";
}

std::string Trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) --end;
    return text.substr(begin, end - begin);
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

bool SameText(const std::string& left, const std::string& right) {
    return Lower(Trim(left)) == Lower(Trim(right));
}

std::string Join(const std::vector<std::string>& parts, const std::string& separator) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) out += separator;
        out += parts[i];
    }
    return out;
}

std::string Qualified(Engine engine, const std::string& scope, const std::string& name) {
    std::string effective = scope;
    if (effective.empty()) {
        effective = engine == Engine::Postgres ? "public" : "main";
    }
    return QuoteIdentifier(engine, effective) + "." + QuoteIdentifier(engine, name);
}

std::string Pragma(Engine, const std::string& scope, const char* name,
                   const std::string& argument) {
    std::string effective = scope.empty() ? "main" : scope;
    return "PRAGMA " + QuoteIdentifier(Engine::Sqlite, effective) + "." + name + "(" +
           QuoteStringLiteral(argument) + ")";
}

bool SafeExpression(const std::string& value, const std::string& field, std::string& error) {
    if (value.find(';') != std::string::npos || value.find("--") != std::string::npos ||
        value.find("/*") != std::string::npos || value.find("*/") != std::string::npos) {
        error = field + " 含有不允许的字符（; 或注释符号）";
        return false;
    }
    for (unsigned char ch : value) {
        if (ch == '\n' || ch == '\r' || ch < 0x20) {
            error = field + " 含有控制字符";
            return false;
        }
    }
    return true;
}

bool IsValidUtf8(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80) {
            ++i;
            continue;
        }
        std::size_t extra = 0;
        if ((lead & 0xE0) == 0xC0) {
            extra = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            extra = 2;
        } else if ((lead & 0xF8) == 0xF0) {
            extra = 3;
        } else {
            return false;
        }
        if (i + extra >= text.size()) return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

std::size_t IdentifierLimit(Engine engine) {
    if (engine == Engine::MySql) return 64;
    if (engine == Engine::Postgres) return 63;
    return 255;
}

// 表设计器的新对象名做严格校验：UTF-8、长度、控制字符、引号、分号。
bool SafeName(const std::string& value, const std::string& field, bool allow_empty,
              std::string& error, std::size_t max_bytes = 255) {
    if (value.empty()) {
        if (allow_empty) return true;
        error = field + "不能为空";
        return false;
    }
    if (!IsValidUtf8(value)) {
        error = field + " 不是有效的 UTF-8 文本";
        return false;
    }
    if (value.size() > max_bytes) {
        error = field + " 过长（最多 " + std::to_string(max_bytes) + " 字节）";
        return false;
    }
    for (unsigned char ch : value) {
        if (ch < 0x20 || ch == 0x7F) {
            error = field + " 含有控制字符";
            return false;
        }
    }
    if (value.find('"') != std::string::npos || value.find('`') != std::string::npos ||
        value.find('\'') != std::string::npos) {
        error = field + " 不能包含引号";
        return false;
    }
    if (value.find(';') != std::string::npos) {
        error = field + " 不能包含分号";
        return false;
    }
    return true;
}

struct TypeShape {
    std::string base;
    std::vector<long long> sizes;
};

std::string NormalizeTypeBase(std::string text) {
    text = Lower(Trim(text));
    // 去掉 MySQL 的列修饰，避免 "INT UNSIGNED" 与 "INT" 被误判为不同类型。
    for (const char* suffix : {" unsigned", " zerofill"}) {
        const std::string tail = suffix;
        if (text.size() > tail.size() &&
            text.compare(text.size() - tail.size(), tail.size(), tail) == 0) {
            text.resize(text.size() - tail.size());
            text = Trim(text);
        }
    }
    static const std::map<std::string, std::string> synonyms = {
        {"integer", "int"},        {"int4", "int"},
        {"int8", "bigint"},        {"int2", "smallint"},
        {"character varying", "varchar"}, {"character", "char"},
        {"numeric", "decimal"},    {"double precision", "double"},
        {"float8", "double"},      {"float4", "real"},
        {"bool", "boolean"},       {"timestamp without time zone", "timestamp"},
        {"time without time zone", "time"},
    };
    const auto found = synonyms.find(text);
    return found == synonyms.end() ? text : found->second;
}

TypeShape ParseTypeShape(const std::string& type) {
    TypeShape shape;
    const std::string text = Lower(Trim(type));
    const std::size_t paren = text.find('(');
    shape.base = NormalizeTypeBase(paren == std::string::npos ? text : text.substr(0, paren));
    if (paren == std::string::npos) return shape;

    const std::size_t close = text.find(')', paren);
    const std::string args =
        text.substr(paren + 1, close == std::string::npos ? std::string::npos : close - paren - 1);
    std::size_t start = 0;
    while (start <= args.size()) {
        const std::size_t comma = args.find(',', start);
        const std::string part =
            Trim(args.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (!part.empty()) {
            char* end = nullptr;
            const long long value = std::strtoll(part.c_str(), &end, 10);
            shape.sizes.push_back(end != nullptr && *end == '\0' ? value : -1);
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return shape;
}

// SQL 预览警告：改类型/缩短长度/收紧非空都可能丢数据或失败。
void AddColumnChangeWarnings(const DdlColumn& before, const DdlColumn& after, DdlPlan& plan) {
    const TypeShape old_shape = ParseTypeShape(before.type);
    const TypeShape new_shape = ParseTypeShape(after.type);
    if (old_shape.base != new_shape.base) {
        plan.warnings.push_back("列 " + after.name + " 类型从 " + Trim(before.type) + " 改为 " +
                                Trim(after.type) + "，可能丢失或截断数据");
    } else if (!SameText(before.type, after.type)) {
        const bool old_known = !old_shape.sizes.empty() && old_shape.sizes.front() > 0;
        const bool new_known = !new_shape.sizes.empty() && new_shape.sizes.front() > 0;
        if (old_known && new_known && new_shape.sizes.front() < old_shape.sizes.front()) {
            plan.warnings.push_back("列 " + after.name + " 长度从 " + Trim(before.type) +
                                    " 缩短为 " + Trim(after.type) + "，可能截断数据");
        } else if (!(old_known && new_known && new_shape.sizes.front() > old_shape.sizes.front()) &&
                   old_shape.sizes != new_shape.sizes) {
            plan.warnings.push_back("列 " + after.name + " 类型从 " + Trim(before.type) + " 改为 " +
                                    Trim(after.type) + "，可能丢失数据");
        }
    }
    if (!before.not_null && after.not_null) {
        plan.warnings.push_back("列 " + after.name + " 改为 NOT NULL，表中已有 NULL 值时将执行失败");
    }
}

bool HasDefault(const DdlColumn& column) {
    return column.has_default && !Trim(column.default_expr).empty();
}

std::string ColumnTypeSql(const DdlColumn& column) { return Trim(column.type); }

std::string ColumnDefinitionSql(Engine engine, const DdlColumn& column) {
    std::string sql = QuoteIdentifier(engine, column.name) + " " + ColumnTypeSql(column);
    if (engine == Engine::Postgres && column.auto_increment && !HasDefault(column)) {
        sql += " GENERATED BY DEFAULT AS IDENTITY";
    }
    if (column.not_null) sql += " NOT NULL";
    if (HasDefault(column)) sql += " DEFAULT " + Trim(column.default_expr);
    if (engine == Engine::MySql && column.auto_increment) sql += " AUTO_INCREMENT";
    if (engine == Engine::MySql && !column.comment.empty()) {
        sql += " COMMENT " + QuoteStringLiteral(column.comment);
    }
    return sql;
}

std::vector<std::string> PrimaryKeyColumns(const DdlTableDefinition& definition) {
    std::vector<std::string> keys;
    for (const auto& column : definition.columns) {
        if (column.primary_key) keys.push_back(column.name);
    }
    return keys;
}

std::string QuotedList(Engine engine, const std::vector<std::string>& columns) {
    std::vector<std::string> quoted;
    quoted.reserve(columns.size());
    for (const auto& column : columns) quoted.push_back(QuoteIdentifier(engine, column));
    return Join(quoted, ", ");
}

std::string AutoConstraintName(const std::string& prefix, const std::string& table,
                               const std::vector<std::string>& columns,
                               const std::set<std::string>& taken) {
    std::string base = prefix + "_" + table;
    if (!columns.empty()) base += "_" + columns.front();
    std::string safe;
    for (unsigned char ch : base) {
        safe.push_back(std::isalnum(ch) != 0 || ch == '_' ? static_cast<char>(ch) : '_');
    }
    if (safe.size() > 60) safe.resize(60);
    std::string name = safe;
    int suffix = 2;
    while (taken.count(name) != 0) {
        name = safe + "_" + std::to_string(suffix);
        ++suffix;
    }
    return name;
}

std::string IndexName(const DdlTableDefinition& definition, const DdlIndex& index,
                      const std::set<std::string>& taken) {
    return index.name.empty() ? AutoConstraintName("idx", definition.name, index.columns, taken)
                              : index.name;
}

std::string ForeignKeyName(const DdlTableDefinition& definition, const DdlForeignKey& fk,
                           const std::set<std::string>& taken) {
    return fk.name.empty() ? AutoConstraintName("fk", definition.name, fk.columns, taken) : fk.name;
}

std::string FkReference(Engine engine, const DdlTableDefinition& definition,
                        const DdlForeignKey& fk) {
    if (engine == Engine::Sqlite) {
        return QuoteIdentifier(engine, fk.ref_table);
    }
    const std::string scope = fk.ref_schema.empty() ? definition.scope : fk.ref_schema;
    return Qualified(engine, scope, fk.ref_table);
}

std::string ForeignKeySql(Engine engine, const DdlTableDefinition& definition,
                          const DdlForeignKey& fk, const std::string& name) {
    std::string sql = "CONSTRAINT " + QuoteIdentifier(engine, name) + " FOREIGN KEY (" +
                      QuotedList(engine, fk.columns) + ") REFERENCES " +
                      FkReference(engine, definition, fk) + " (" +
                      QuotedList(engine, fk.ref_columns) + ")";
    if (!fk.on_delete.empty() && Upper(Trim(fk.on_delete)) != "NO ACTION") {
        sql += " ON DELETE " + Upper(Trim(fk.on_delete));
    }
    if (!fk.on_update.empty() && Upper(Trim(fk.on_update)) != "NO ACTION") {
        sql += " ON UPDATE " + Upper(Trim(fk.on_update));
    }
    return sql;
}

std::string CreateIndexSql(Engine engine, const std::string& scope, const std::string& table,
                           const DdlIndex& index, const std::string& name) {
    const std::string unique = index.unique ? "UNIQUE " : "";
    const std::string columns = QuotedList(engine, index.columns);
    if (engine == Engine::MySql) {
        return "CREATE " + unique + "INDEX " + QuoteIdentifier(engine, name) + " ON " +
               Qualified(engine, scope, table) + " (" + columns + ")";
    }
    if (engine == Engine::Sqlite) {
        const std::string effective = scope.empty() ? "main" : scope;
        return "CREATE " + unique + "INDEX " + QuoteIdentifier(engine, effective) + "." +
               QuoteIdentifier(engine, name) + " ON " + QuoteIdentifier(engine, table) + " (" +
               columns + ")";
    }
    return "CREATE " + unique + "INDEX " + QuoteIdentifier(engine, name) + " ON " +
           Qualified(engine, scope, table) + " (" + columns + ")";
}

std::string DropIndexSql(Engine engine, const std::string& scope, const std::string& table,
                         const std::string& name) {
    if (engine == Engine::MySql) {
        return "DROP INDEX " + QuoteIdentifier(engine, name) + " ON " +
               Qualified(engine, scope, table);
    }
    return "DROP INDEX " + Qualified(engine, scope, name);
}

std::string MysqlTableOptions(const DdlTableOptions& options) {
    std::vector<std::string> parts;
    if (!options.engine.empty()) parts.push_back("ENGINE=" + options.engine);
    if (!options.charset.empty()) parts.push_back("DEFAULT CHARSET=" + options.charset);
    if (!options.collation.empty()) parts.push_back("COLLATE=" + options.collation);
    if (!options.comment.empty()) {
        parts.push_back("COMMENT=" + QuoteStringLiteral(options.comment));
    }
    return Join(parts, " ");
}

bool SameColumn(const DdlColumn& left, const DdlColumn& right) {
    return SameText(left.type, right.type) && left.not_null == right.not_null &&
           left.auto_increment == right.auto_increment && HasDefault(left) == HasDefault(right) &&
           (!HasDefault(left) || Trim(left.default_expr) == Trim(right.default_expr)) &&
           left.comment == right.comment;
}

bool SameIndex(const DdlIndex& left, const DdlIndex& right) {
    if (left.unique != right.unique || left.columns.size() != right.columns.size()) return false;
    for (std::size_t i = 0; i < left.columns.size(); ++i) {
        if (left.columns[i] != right.columns[i]) return false;
    }
    return true;
}

bool SameForeignKey(const DdlForeignKey& left, const DdlForeignKey& right) {
    return left.columns == right.columns && left.ref_table == right.ref_table &&
           left.ref_columns == right.ref_columns &&
           Upper(Trim(left.on_update)) == Upper(Trim(right.on_update)) &&
           Upper(Trim(left.on_delete)) == Upper(Trim(right.on_delete));
}

bool AppendCreate(Engine engine, const DdlTableDefinition& definition, DdlPlan& plan,
                  std::string& error) {
    const std::vector<std::string> primary_keys = PrimaryKeyColumns(definition);
    std::vector<std::string> lines;
    std::set<std::string> used_names;

    const bool sqlite_inline_pk = engine == Engine::Sqlite && primary_keys.size() == 1;
    for (const auto& column : definition.columns) {
        std::string line = ColumnDefinitionSql(engine, column);
        if (sqlite_inline_pk && column.primary_key) {
            line += " PRIMARY KEY";
            if (column.auto_increment && Upper(Trim(column.type)) == "INTEGER") {
                line += " AUTOINCREMENT";
            }
            if (column.auto_increment && Upper(Trim(column.type)) != "INTEGER") {
                plan.warnings.push_back("SQLite 自增要求列类型为 INTEGER，列 " + column.name +
                                        " 未生成 AUTOINCREMENT");
            }
        }
        lines.push_back(std::move(line));
    }

    if (!primary_keys.empty() && !sqlite_inline_pk) {
        if (engine == Engine::MySql) {
            lines.push_back("PRIMARY KEY (" + QuotedList(engine, primary_keys) + ")");
        } else {
            const std::string name = definition.primary_key_name.empty()
                                         ? definition.name + "_pkey"
                                         : definition.primary_key_name;
            lines.push_back("CONSTRAINT " + QuoteIdentifier(engine, name) + " PRIMARY KEY (" +
                            QuotedList(engine, primary_keys) + ")");
        }
    }

    std::vector<std::pair<const DdlIndex*, std::string>> index_names;
    for (const auto& index : definition.indexes) {
        if (index.columns.empty()) {
            error = "索引 " + index.name + " 没有列";
            return false;
        }
        const std::string name = IndexName(definition, index, used_names);
        used_names.insert(name);
        index_names.emplace_back(&index, name);
        if (engine == Engine::MySql) {
            lines.push_back(std::string(index.unique ? "UNIQUE KEY " : "KEY ") +
                            QuoteIdentifier(engine, name) + " (" +
                            QuotedList(engine, index.columns) + ")");
        }
    }

    for (const auto& fk : definition.foreign_keys) {
        const std::string name = ForeignKeyName(definition, fk, used_names);
        used_names.insert(name);
        lines.push_back(ForeignKeySql(engine, definition, fk, name));
    }

    std::string sql = "CREATE TABLE " + Qualified(engine, definition.scope, definition.name) +
                      " (\n  " + Join(lines, ",\n  ") + "\n)";
    if (engine == Engine::MySql) {
        const std::string options = MysqlTableOptions(definition.options);
        if (!options.empty()) sql += " " + options;
    }
    plan.statements.push_back(sql + ";");

    if (engine != Engine::MySql) {
        for (const auto& entry : index_names) {
            plan.statements.push_back(
                CreateIndexSql(engine, definition.scope, definition.name, *entry.first, entry.second));
        }
    }

    if (engine == Engine::Postgres) {
        if (!definition.options.comment.empty()) {
            plan.statements.push_back("COMMENT ON TABLE " +
                                      Qualified(engine, definition.scope, definition.name) +
                                      " IS " + QuoteStringLiteral(definition.options.comment) +
                                      ";");
        }
        for (const auto& column : definition.columns) {
            if (column.comment.empty()) continue;
            plan.statements.push_back(
                "COMMENT ON COLUMN " +
                Qualified(engine, definition.scope, definition.name) + "." +
                QuoteIdentifier(engine, column.name) + " IS " +
                QuoteStringLiteral(column.comment) + ";");
        }
    }

    if (engine == Engine::Sqlite) {
        for (const auto& column : definition.columns) {
            if (!column.comment.empty()) {
                plan.warnings.push_back("SQLite 不支持列注释，已忽略列 " + column.name + " 的注释");
            }
        }
        if (!definition.options.comment.empty()) {
            plan.warnings.push_back("SQLite 不支持表注释，已忽略表注释");
        }
        if (!definition.options.charset.empty() || !definition.options.collation.empty() ||
            !definition.options.engine.empty()) {
            plan.warnings.push_back("SQLite 不支持引擎/字符集选项，已忽略");
        }
    }
    if (engine == Engine::Postgres) {
        if (!definition.options.charset.empty() || !definition.options.collation.empty() ||
            !definition.options.engine.empty()) {
            plan.warnings.push_back("PostgreSQL 忽略字符集/存储引擎选项");
        }
    }
    return true;
}

bool AppendAlter(Engine engine, const std::string& scope, const DdlTableDefinition& original,
                 const DdlTableDefinition& updated, DdlPlan& plan, std::string& error) {
    if (original.scope != updated.scope) {
        error = "不支持修改表所属的库/Schema";
        return false;
    }

    std::string table = original.name;

    // 1. 表重命名
    if (updated.name != original.name) {
        const std::string old_qualified = Qualified(engine, scope, original.name);
        if (engine == Engine::MySql) {
            plan.statements.push_back("RENAME TABLE " + old_qualified + " TO " +
                                      Qualified(engine, scope, updated.name) + ";");
        } else {
            plan.statements.push_back("ALTER TABLE " + old_qualified + " RENAME TO " +
                                      QuoteIdentifier(engine, updated.name) + ";");
        }
        table = updated.name;
    }
    const std::string qualified = Qualified(engine, scope, table);

    // 2. 删除或修改外键（先于索引与列，否则 MySQL 会因外键依赖拒绝 DROP INDEX/COLUMN）
    for (const auto& fk : original.foreign_keys) {
        const DdlForeignKey* replacement = nullptr;
        for (const auto& candidate : updated.foreign_keys) {
            if (candidate.name == fk.name) {
                replacement = &candidate;
                break;
            }
        }
        if (replacement != nullptr && SameForeignKey(fk, *replacement)) {
            continue;
        }
        if (engine == Engine::Sqlite) {
            error = "SQLite 不支持修改外键约束，请重建表";
            return false;
        }
        if (engine == Engine::MySql) {
            plan.statements.push_back("ALTER TABLE " + qualified + " DROP FOREIGN KEY " +
                                      QuoteIdentifier(engine, fk.name) + ";");
        } else {
            plan.statements.push_back("ALTER TABLE " + qualified + " DROP CONSTRAINT " +
                                      QuoteIdentifier(engine, fk.name) + ";");
        }
    }

    // 3. 删除或修改索引（先于删除列）
    for (const auto& index : original.indexes) {
        const DdlIndex* replacement = nullptr;
        for (const auto& candidate : updated.indexes) {
            if (candidate.name == index.name) {
                replacement = &candidate;
                break;
            }
        }
        if (replacement != nullptr && SameIndex(index, *replacement)) {
            continue;
        }
        plan.statements.push_back(DropIndexSql(engine, scope, table, index.name));
    }

    // 4. 列匹配
    std::vector<bool> used(original.columns.size(), false);
    struct Match {
        const DdlColumn* before = nullptr;
        const DdlColumn* after = nullptr;
        std::size_t index = 0;
    };
    std::vector<Match> matches;
    std::vector<const DdlColumn*> additions;
    for (const auto& column : updated.columns) {
        const std::string lookup = column.old_name.empty() ? column.name : column.old_name;
        bool found = false;
        for (std::size_t i = 0; i < original.columns.size(); ++i) {
            if (used[i] || original.columns[i].name != lookup) continue;
            used[i] = true;
            matches.push_back(Match{&original.columns[i], &column, i});
            found = true;
            break;
        }
        if (!found) additions.push_back(&column);
    }

    std::vector<const DdlColumn*> drops;
    for (std::size_t i = 0; i < original.columns.size(); ++i) {
        if (!used[i]) drops.push_back(&original.columns[i]);
    }

    // 5. 列重命名（SQLite / PostgreSQL；MySQL 用 CHANGE COLUMN 合并处理）
    if (engine != Engine::MySql) {
        for (const auto& match : matches) {
            if (match.before->name == match.after->name) continue;
            plan.statements.push_back("ALTER TABLE " + qualified + " RENAME COLUMN " +
                                      QuoteIdentifier(engine, match.before->name) + " TO " +
                                      QuoteIdentifier(engine, match.after->name) + ";");
        }
    }

    // 6. 删除列
    for (const auto* column : drops) {
        plan.warnings.push_back("删除列 " + column->name + " 会丢失该列数据");
        plan.statements.push_back("ALTER TABLE " + qualified + " DROP COLUMN " +
                                  QuoteIdentifier(engine, column->name) + ";");
    }

    // 7. 新增列
    for (const auto* column : additions) {
        if (engine == Engine::Sqlite && column->not_null && !HasDefault(*column)) {
            error = "SQLite 新增列 " + column->name + " 为 NOT NULL 时必须提供默认值";
            return false;
        }
        if (column->not_null && !HasDefault(*column)) {
            plan.warnings.push_back("新增列 " + column->name +
                                    " 为 NOT NULL 且无默认值，表中已有数据时将执行失败");
        }
        plan.statements.push_back("ALTER TABLE " + qualified + " ADD COLUMN " +
                                  ColumnDefinitionSql(engine, *column) + ";");
    }

    // 8. 修改列
    for (const auto& match : matches) {
        const bool renamed = match.before->name != match.after->name;
        const bool changed = !SameColumn(*match.before, *match.after);
        if (!renamed && !changed) continue;

        if (engine == Engine::Sqlite) {
            if (changed) {
                error = "SQLite 不支持修改列定义（列 " + match.before->name +
                        "），请使用重建表";
                return false;
            }
            continue;
        }

        if (changed) AddColumnChangeWarnings(*match.before, *match.after, plan);

        if (engine == Engine::MySql) {
            const std::string keyword = renamed ? " CHANGE COLUMN " : " MODIFY COLUMN ";
            std::string sql = "ALTER TABLE " + qualified + keyword;
            if (renamed) {
                sql += QuoteIdentifier(engine, match.before->name) + " ";
            }
            sql += ColumnDefinitionSql(engine, *match.after) + ";";
            plan.statements.push_back(std::move(sql));
            continue;
        }

        // PostgreSQL：逐项 ALTER COLUMN
        const std::string column_name = QuoteIdentifier(engine, match.after->name);
        if (!SameText(match.before->type, match.after->type)) {
            plan.statements.push_back("ALTER TABLE " + qualified + " ALTER COLUMN " +
                                      column_name + " TYPE " + ColumnTypeSql(*match.after) + ";");
        }
        if (match.before->not_null != match.after->not_null) {
            plan.statements.push_back("ALTER TABLE " + qualified + " ALTER COLUMN " + column_name +
                                      (match.after->not_null ? " SET NOT NULL;" : " DROP NOT NULL;"));
        }
        if (HasDefault(*match.before) != HasDefault(*match.after) ||
            (HasDefault(*match.before) &&
             Trim(match.before->default_expr) != Trim(match.after->default_expr))) {
            if (HasDefault(*match.after)) {
                plan.statements.push_back("ALTER TABLE " + qualified + " ALTER COLUMN " +
                                          column_name + " SET DEFAULT " +
                                          Trim(match.after->default_expr) + ";");
            } else {
                plan.statements.push_back("ALTER TABLE " + qualified + " ALTER COLUMN " +
                                          column_name + " DROP DEFAULT;");
            }
        }
        if (match.before->auto_increment != match.after->auto_increment) {
            if (match.after->auto_increment) {
                plan.statements.push_back("ALTER TABLE " + qualified + " ALTER COLUMN " +
                                          column_name +
                                          " ADD GENERATED BY DEFAULT AS IDENTITY;");
            } else {
                plan.statements.push_back("ALTER TABLE " + qualified + " ALTER COLUMN " +
                                          column_name + " DROP IDENTITY IF EXISTS;");
            }
        }
        if (match.before->comment != match.after->comment) {
            plan.statements.push_back(
                "COMMENT ON COLUMN " + qualified + "." + column_name + " IS " +
                QuoteStringLiteral(match.after->comment) + ";");
        }
    }

    // 9. 主键变更
    const std::vector<std::string> old_pk = PrimaryKeyColumns(original);
    const std::vector<std::string> new_pk = PrimaryKeyColumns(updated);
    if (old_pk != new_pk) {
        if (engine == Engine::Sqlite) {
            error = "SQLite 不支持修改主键，请重建表";
            return false;
        }
        if (engine == Engine::MySql) {
            if (!old_pk.empty()) {
                plan.statements.push_back("ALTER TABLE " + qualified + " DROP PRIMARY KEY;");
            }
            if (!new_pk.empty()) {
                plan.statements.push_back("ALTER TABLE " + qualified + " ADD PRIMARY KEY (" +
                                          QuotedList(engine, new_pk) + ");");
            }
        } else {
            if (!old_pk.empty()) {
                const std::string constraint = original.primary_key_name.empty()
                                                   ? original.name + "_pkey"
                                                   : original.primary_key_name;
                plan.statements.push_back("ALTER TABLE " + qualified + " DROP CONSTRAINT " +
                                          QuoteIdentifier(engine, constraint) + ";");
            }
            if (!new_pk.empty()) {
                plan.statements.push_back("ALTER TABLE " + qualified + " ADD CONSTRAINT " +
                                          QuoteIdentifier(engine, updated.name + "_pkey") +
                                          " PRIMARY KEY (" + QuotedList(engine, new_pk) + ");");
            }
        }
    }

    // 10. 新增索引
    std::set<std::string> used_names;
    for (const auto& index : updated.indexes) used_names.insert(index.name);
    for (const auto& index : updated.indexes) {
        bool existed = false;
        for (const auto& before : original.indexes) {
            if (before.name == index.name && SameIndex(before, index)) {
                existed = true;
                break;
            }
        }
        if (existed) continue;
        if (index.columns.empty()) {
            error = "索引 " + index.name + " 没有列";
            return false;
        }
        const std::string name = IndexName(updated, index, used_names);
        used_names.insert(name);
        if (index.unique) {
            plan.warnings.push_back("唯一索引 " + name + " 在存在重复值时可能创建失败");
        }
        plan.statements.push_back(CreateIndexSql(engine, scope, updated.name, index, name));
    }

    // 11. 新增外键
    std::set<std::string> used_fk_names;
    for (const auto& fk : updated.foreign_keys) used_fk_names.insert(fk.name);
    for (const auto& fk : updated.foreign_keys) {
        bool existed = false;
        for (const auto& before : original.foreign_keys) {
            if (before.name == fk.name && SameForeignKey(before, fk)) {
                existed = true;
                break;
            }
        }
        if (existed) continue;
        if (engine == Engine::Sqlite) {
            error = "SQLite 不支持新增外键约束，请重建表";
            return false;
        }
        const std::string name = ForeignKeyName(updated, fk, used_fk_names);
        used_fk_names.insert(name);
        plan.statements.push_back("ALTER TABLE " + qualified + " ADD " +
                                  ForeignKeySql(engine, updated, fk, name) + ";");
    }

    // 12. 表选项/注释
    if (engine == Engine::MySql) {
        const std::string options = MysqlTableOptions(updated.options);
        if (options != MysqlTableOptions(original.options) && !options.empty()) {
            plan.statements.push_back("ALTER TABLE " + qualified + " " + options + ";");
        }
    } else if (engine == Engine::Postgres) {
        if (original.options.comment != updated.options.comment) {
            plan.statements.push_back(
                "COMMENT ON TABLE " + qualified + " IS " +
                QuoteStringLiteral(updated.options.comment) + ";");
        }
        if (!updated.options.charset.empty() || !updated.options.collation.empty() ||
            !updated.options.engine.empty()) {
            plan.warnings.push_back("PostgreSQL 忽略字符集/存储引擎选项");
        }
    } else {
        if (original.options.comment != updated.options.comment &&
            (!updated.options.comment.empty() || !original.options.comment.empty())) {
            plan.warnings.push_back("SQLite 不支持表注释，已忽略");
        }
    }

    if (plan.statements.empty() && plan.warnings.empty()) {
        plan.warnings.push_back("没有检测到需要执行的变更");
    }
    return true;
}

void LoadSqliteIndexes(Connection& connection, const std::string& scope, const std::string& table,
                       DdlTableDefinition& out) {
    auto list = Run(connection, Pragma(Engine::Sqlite, scope, "index_list", table));
    if (!list.ok()) return;
    for (const auto& row : list.rows) {
        const std::string name = RowText(row, 1);
        const std::string origin = RowText(row, 3);
        if (origin == "pk") continue;
        if (name.rfind("sqlite_autoindex", 0) == 0) continue;  // 内联 UNIQUE 自动索引不可 DROP
        if (RowInt(row, 4, 0) != 0) continue;                // 部分索引不进入设计器
        auto info = Run(connection, Pragma(Engine::Sqlite, scope, "index_info", name));
        if (!info.ok()) continue;
        DdlIndex index;
        index.name = name;
        index.unique = RowInt(row, 2, 0) != 0;
        bool broken = false;
        for (const auto& info_row : info.rows) {
            const std::string column = RowText(info_row, 2);
            if (column.empty()) {
                broken = true;  // 表达式索引
                break;
            }
            index.columns.push_back(column);
        }
        if (!broken && !index.columns.empty()) out.indexes.push_back(std::move(index));
    }
}

void LoadSqliteForeignKeys(Connection& connection, const std::string& scope,
                           const std::string& table, DdlTableDefinition& out) {
    auto rows = Run(connection, Pragma(Engine::Sqlite, scope, "foreign_key_list", table));
    if (!rows.ok()) return;
    std::map<std::int64_t, DdlForeignKey> grouped;
    std::map<std::int64_t, std::vector<std::pair<std::int64_t, std::string>>> ref_columns;
    for (const auto& row : rows.rows) {
        const std::int64_t id = RowInt(row, 0, 0);
        const std::int64_t seq = RowInt(row, 1, 0);
        DdlForeignKey& fk = grouped[id];
        fk.ref_table = RowText(row, 2);
        fk.columns.resize(std::max<std::size_t>(fk.columns.size(),
                                                static_cast<std::size_t>(seq) + 1));
        fk.columns[static_cast<std::size_t>(seq)] = RowText(row, 3);
        fk.on_update = RowText(row, 5);
        fk.on_delete = RowText(row, 6);
        ref_columns[id].emplace_back(seq, RowText(row, 4));
    }
    for (auto& item : grouped) {
        auto& columns = ref_columns[item.first];
        std::sort(columns.begin(), columns.end(),
                  [](const auto& left, const auto& right) { return left.first < right.first; });
        for (const auto& entry : columns) item.second.ref_columns.push_back(entry.second);
        item.second.name = "fk_" + table + "_" + std::to_string(item.first);
        out.foreign_keys.push_back(std::move(item.second));
    }
}

void LoadEngineIndexes(Connection& connection, const std::string& scope, const std::string& table,
                       DdlTableDefinition& out, bool postgres) {
    std::string sql;
    if (!postgres) {
        sql = ListIndexesSql(Engine::MySql, scope, table);
    } else {
        sql = "SELECT i.relname, a.attname, ix.indisunique, ix.indisprimary"
              " FROM pg_index ix"
              " JOIN pg_class c ON c.oid = ix.indrelid"
              " JOIN pg_class i ON i.oid = ix.indexrelid"
              " JOIN pg_namespace n ON n.oid = c.relnamespace"
              " JOIN LATERAL unnest(ix.indkey) WITH ORDINALITY AS u(attnum, ord) ON TRUE"
              " JOIN pg_attribute a ON a.attrelid = ix.indrelid AND a.attnum = u.attnum"
              " WHERE n.nspname = " + QuoteStringLiteral(scope) +
              " AND c.relname = " + QuoteStringLiteral(table) +
              " ORDER BY i.relname, u.ord";
    }
    auto rows = Run(connection, sql);
    if (!rows.ok()) return;

    std::map<std::string, DdlIndex> grouped;
    std::map<std::string, std::vector<std::pair<std::int64_t, std::string>>> order;
    std::set<std::string> primary_indexes;
    for (const auto& row : rows.rows) {
        const std::string name = RowText(row, 0);
        if (name == "PRIMARY") continue;
        if (postgres && IsTrue(RowText(row, 3))) {
            primary_indexes.insert(name);
            continue;
        }
        DdlIndex& index = grouped[name];
        index.name = name;
        if (!postgres) {
            index.unique = RowText(row, 2) == "0";
            order[name].emplace_back(RowInt(row, 3, 0), RowText(row, 1));
        } else {
            index.unique = IsTrue(RowText(row, 2));
            order[name].emplace_back(0, RowText(row, 1));
        }
    }
    for (auto& item : grouped) {
        auto& columns = order[item.first];
        std::sort(columns.begin(), columns.end(),
                  [](const auto& left, const auto& right) { return left.first < right.first; });
        for (const auto& entry : columns) item.second.columns.push_back(entry.second);
        if (!item.second.columns.empty()) out.indexes.push_back(std::move(item.second));
    }
}

void LoadEngineForeignKeys(Connection& connection, const std::string& scope,
                           const std::string& table, DdlTableDefinition& out, bool postgres) {
    std::string sql;
    if (!postgres) {
        sql = ListForeignKeysSql(Engine::MySql, scope, table);
    } else {
        sql = "SELECT con.conname,"
              " (SELECT string_agg(a.attname, ',' ORDER BY u.ord)"
              "    FROM unnest(con.conkey) WITH ORDINALITY AS u(attnum, ord)"
              "    JOIN pg_attribute a ON a.attrelid = con.conrelid AND a.attnum = u.attnum),"
              " rn.nspname, rc.relname,"
              " (SELECT string_agg(a.attname, ',' ORDER BY u.ord)"
              "    FROM unnest(con.confkey) WITH ORDINALITY AS u(attnum, ord)"
              "    JOIN pg_attribute a ON a.attrelid = con.confrelid AND a.attnum = u.attnum),"
              " con.confupdtype::text, con.confdeltype::text"
              " FROM pg_constraint con"
              " JOIN pg_class c ON c.oid = con.conrelid"
              " JOIN pg_namespace n ON n.oid = c.relnamespace"
              " JOIN pg_class rc ON rc.oid = con.confrelid"
              " JOIN pg_namespace rn ON rn.oid = rc.relnamespace"
              " WHERE n.nspname = " + QuoteStringLiteral(scope) +
              " AND c.relname = " + QuoteStringLiteral(table) +
              " AND con.contype = 'f' ORDER BY con.conname";
    }
    auto rows = Run(connection, sql);
    if (!rows.ok()) return;

    auto split = [](const std::string& text) {
        std::vector<std::string> parts;
        std::size_t start = 0;
        for (;;) {
            const std::size_t comma = text.find(',', start);
            const std::string part = Trim(text.substr(
                start, comma == std::string::npos ? std::string::npos : comma - start));
            if (!part.empty()) parts.push_back(part);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        return parts;
    };
    auto action = [](const std::string& code) -> std::string {
        if (code == "r") return "RESTRICT";
        if (code == "c") return "CASCADE";
        if (code == "n") return "SET NULL";
        if (code == "d") return "SET DEFAULT";
        return "NO ACTION";
    };

    if (postgres) {
        for (const auto& row : rows.rows) {
            DdlForeignKey fk;
            fk.name = RowText(row, 0);
            fk.columns = split(RowText(row, 1));
            fk.ref_schema = RowText(row, 2);
            fk.ref_table = RowText(row, 3);
            fk.ref_columns = split(RowText(row, 4));
            fk.on_update = action(RowText(row, 5));
            fk.on_delete = action(RowText(row, 6));
            out.foreign_keys.push_back(std::move(fk));
        }
        return;
    }

    std::map<std::string, DdlForeignKey> grouped;
    for (const auto& row : rows.rows) {
        DdlForeignKey& fk = grouped[RowText(row, 0)];
        fk.name = RowText(row, 0);
        fk.columns.push_back(RowText(row, 1));
        fk.ref_table = RowText(row, 2);
        fk.ref_columns.push_back(RowText(row, 3));
        fk.on_update = RowText(row, 4);
        fk.on_delete = RowText(row, 5);
    }
    for (auto& item : grouped) out.foreign_keys.push_back(std::move(item.second));
}

void LoadOptions(Connection& connection, const std::string& scope, const std::string& table,
                 DdlTableDefinition& out, bool postgres) {
    if (!postgres) {
        auto rows = Run(connection,
                        "SELECT ENGINE, TABLE_COLLATION, TABLE_COMMENT"
                        " FROM information_schema.TABLES WHERE TABLE_SCHEMA = " +
                            QuoteStringLiteral(scope) + " AND TABLE_NAME = " +
                            QuoteStringLiteral(table));
        if (rows.ok() && !rows.rows.empty()) {
            const auto& row = rows.rows.front();
            out.options.engine = RowText(row, 0);
            out.options.collation = RowText(row, 1);
            const std::size_t underscore = out.options.collation.find('_');
            out.options.charset = underscore == std::string::npos
                                      ? std::string{}
                                      : out.options.collation.substr(0, underscore);
            out.options.comment = RowText(row, 2);
        }
        out.primary_key_name = "PRIMARY";
        return;
    }

    auto rows = Run(connection,
                    "SELECT obj_description(c.oid, 'pg_class'),"
                    " (SELECT con.conname FROM pg_constraint con"
                    "   WHERE con.conrelid = c.oid AND con.contype = 'p' LIMIT 1)"
                    " FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace"
                    " WHERE n.nspname = " + QuoteStringLiteral(scope) +
                        " AND c.relname = " + QuoteStringLiteral(table));
    if (rows.ok() && !rows.rows.empty()) {
        out.options.comment = RowText(rows.rows.front(), 0);
        out.primary_key_name = RowText(rows.rows.front(), 1);
    }
}

}  // namespace

DdlMode DdlModeFromName(const std::string& name, bool& ok) {
    ok = true;
    if (name == "create") return DdlMode::Create;
    if (name == "alter") return DdlMode::Alter;
    if (name == "drop") return DdlMode::Drop;
    if (name == "truncate") return DdlMode::Truncate;
    if (name == "database.create") return DdlMode::CreateDatabase;
    if (name == "database.drop") return DdlMode::DropDatabase;
    ok = false;
    return DdlMode::Create;
}

std::vector<std::string> DdlTypeSuggestions(Engine engine) {
    if (engine == Engine::MySql) {
        return {"INT",          "BIGINT",     "SMALLINT",  "TINYINT",   "MEDIUMINT",
                "DECIMAL(10,2)", "FLOAT",     "DOUBLE",    "BIT(1)",    "BOOLEAN",
                "DATE",         "DATETIME",  "TIMESTAMP", "TIME",      "YEAR",
                "CHAR(36)",     "VARCHAR(64)", "VARCHAR(255)", "TEXT",  "MEDIUMTEXT",
                "LONGTEXT",     "JSON",      "BLOB",      "VARBINARY(255)", "ENUM('a','b')"};
    }
    if (engine == Engine::Postgres) {
        return {"integer",       "bigint",        "smallint",     "numeric(10,2)",
                "real",          "double precision", "boolean",   "date",
                "timestamp with time zone", "timestamp without time zone", "time",
                "interval",      "uuid",          "json",         "jsonb",
                "character varying(64)", "character varying(255)", "text",
                "bytea",         "inet",          "cidr",         "macaddr"};
    }
    return {"INTEGER", "TEXT", "REAL", "BLOB", "NUMERIC", "BOOLEAN", "DATE", "DATETIME"};
}

bool ValidateDefinition(const DdlTableDefinition& definition, std::string& error) {
    const std::size_t limit = IdentifierLimit(definition.engine);
    if (!SafeName(definition.name, "表名", false, error, limit)) return false;
    if (!SafeName(definition.scope, "库/Schema", true, error, limit)) return false;
    if (definition.columns.empty()) {
        error = "至少需要一列";
        return false;
    }

    std::set<std::string> columns;
    for (const auto& column : definition.columns) {
        if (!SafeName(column.name, "列名", false, error, limit)) return false;
        if (!columns.insert(Lower(column.name)).second) {
            error = "列名重复: " + column.name;
            return false;
        }
        const std::string type = Trim(column.type);
        if (type.empty()) {
            error = "列 " + column.name + " 缺少类型";
            return false;
        }
        if (!SafeExpression(type, "列 " + column.name + " 的类型", error)) return false;
        if (HasDefault(column) &&
            !SafeExpression(column.default_expr, "列 " + column.name + " 的默认值", error)) {
            return false;
        }
        for (unsigned char ch : column.comment) {
            if (ch < 0x20) {
                error = "列 " + column.name + " 的注释含有控制字符";
                return false;
            }
        }
    }

    std::set<std::string> indexes;
    for (const auto& index : definition.indexes) {
        if (!SafeName(index.name, "索引名", true, error, limit)) return false;
        if (!index.name.empty() && !indexes.insert(Lower(index.name)).second) {
            error = "索引名重复: " + index.name;
            return false;
        }
        if (index.columns.empty()) {
            error = "索引 " + index.name + " 没有列";
            return false;
        }
        for (const auto& column : index.columns) {
            if (columns.count(Lower(column)) == 0) {
                error = "索引引用了不存在的列: " + column;
                return false;
            }
        }
    }

    std::set<std::string> foreign_keys;
    for (const auto& fk : definition.foreign_keys) {
        if (!SafeName(fk.name, "外键名", true, error, limit)) return false;
        if (!fk.name.empty() && !foreign_keys.insert(Lower(fk.name)).second) {
            error = "外键名重复: " + fk.name;
            return false;
        }
        if (!SafeName(fk.ref_table, "引用表", false, error, limit)) return false;
        if (!SafeName(fk.ref_schema, "引用 Schema", true, error, limit)) return false;
        if (fk.columns.empty() || fk.columns.size() != fk.ref_columns.size()) {
            error = "外键 " + fk.name + " 的列与引用列数量不一致";
            return false;
        }
        for (const auto& column : fk.columns) {
            if (columns.count(Lower(column)) == 0) {
                error = "外键引用了不存在的列: " + column;
                return false;
            }
        }
        for (const auto& column : fk.ref_columns) {
            if (!SafeName(column, "外键引用列", false, error, limit)) return false;
        }
    }
    return true;
}

bool LoadDdlDefinition(Connection& connection, const std::string& scope,
                       const std::string& table, DdlTableDefinition& out, DbError& error) {
    out = DdlTableDefinition{};
    out.engine = connection.EngineKind();
    out.scope = scope;
    out.name = table;
    out.exists = true;

    TableRef ref;
    ref.kind = "table";
    ref.name = table;
    if (out.engine == Engine::MySql) {
        ref.database = scope;
    } else if (out.engine == Engine::Postgres) {
        ref.schema = scope;
    } else {
        ref.database = scope;
    }

    TableLayout layout;
    DbError layout_error;
    if (!LoadTableLayout(connection, ref, false, layout, layout_error)) {
        error = layout_error;
        return false;
    }
    out.scope = layout.scope;
    for (const auto& column : layout.columns) {
        DdlColumn item;
        item.name = column.meta.name;
        item.type = column.meta.db_type;
        item.not_null = !column.meta.nullable;
        item.primary_key = column.meta.primary_key;
        item.auto_increment = column.meta.auto_increment;
        item.has_default = !Trim(column.meta.default_expr).empty();
        item.default_expr = column.meta.default_expr;
        item.comment = column.meta.comment;
        out.columns.push_back(std::move(item));
    }

    const bool postgres = out.engine == Engine::Postgres;
    if (out.engine == Engine::Sqlite) {
        LoadSqliteIndexes(connection, out.scope, table, out);
        LoadSqliteForeignKeys(connection, out.scope, table, out);
    } else {
        LoadEngineIndexes(connection, out.scope, table, out, postgres);
        LoadEngineForeignKeys(connection, out.scope, table, out, postgres);
        LoadOptions(connection, out.scope, table, out, postgres);
        if (!postgres) {
            // MySQL 会为没有可用索引的外键自动创建同名索引；它不是用户定义的索引，
            // 设计器不应把它展示为可删除对象，否则 ALTER 会尝试删除外键依赖的索引。
            out.indexes.erase(
                std::remove_if(out.indexes.begin(), out.indexes.end(),
                               [&out](const DdlIndex& index) {
                                   return std::any_of(
                                       out.foreign_keys.begin(), out.foreign_keys.end(),
                                       [&index](const DdlForeignKey& fk) {
                                           return !fk.name.empty() && fk.name == index.name &&
                                                  fk.columns == index.columns;
                                       });
                               }),
                out.indexes.end());
        }
    }
    return true;
}

bool BuildDdlPlan(DdlMode mode, Engine engine, const std::string& scope,
                  const DdlTableDefinition& original, const DdlTableDefinition& updated,
                  const std::string& new_name, DdlPlan& out, std::string& error) {
    out = DdlPlan{};
    error.clear();

    if (mode == DdlMode::CreateDatabase || mode == DdlMode::DropDatabase) {
        const std::string name = Trim(new_name.empty() ? updated.name : new_name);
        if (!SafeName(name, "数据库名", false, error, IdentifierLimit(engine))) return false;
        if (mode == DdlMode::CreateDatabase) {
            if (engine == Engine::MySql) {
                std::string sql = "CREATE DATABASE " + QuoteIdentifier(engine, name);
                if (!updated.options.charset.empty()) {
                    sql += " DEFAULT CHARACTER SET " + updated.options.charset;
                }
                if (!updated.options.collation.empty()) {
                    sql += " COLLATE " + updated.options.collation;
                }
                out.statements.push_back(sql + ";");
            } else if (engine == Engine::Postgres) {
                out.statements.push_back("CREATE DATABASE " + QuoteIdentifier(engine, name) + ";");
                out.warnings.push_back("PostgreSQL 的 CREATE DATABASE 不能在事务中执行");
            } else {
                error = "SQLite 不支持通过设计器创建数据库，请新建数据库文件";
                return false;
            }
        } else {
            if (engine == Engine::Sqlite) {
                error = "SQLite 不支持 DROP DATABASE";
                return false;
            }
            out.statements.push_back("DROP DATABASE " + QuoteIdentifier(engine, name) + ";");
            out.warnings.push_back("删除数据库不可恢复，且会断开其他会话");
        }
        return true;
    }

    if (mode == DdlMode::Drop) {
        DdlTableDefinition target = original.name.empty() ? updated : original;
        if (target.name.empty()) {
            error = "缺少表名";
            return false;
        }
        if (!SafeName(target.name, "表名", false, error, IdentifierLimit(engine))) return false;
        out.statements.push_back("DROP TABLE " + Qualified(engine, scope, target.name) + ";");
        out.warnings.push_back("删除表会同时删除表内数据，且不可恢复");
        return true;
    }

    if (mode == DdlMode::Truncate) {
        DdlTableDefinition target = original.name.empty() ? updated : original;
        if (target.name.empty()) {
            error = "缺少表名";
            return false;
        }
        if (engine == Engine::Sqlite) {
            out.statements.push_back("DELETE FROM " + Qualified(engine, scope, target.name) + ";");
        } else {
            out.statements.push_back("TRUNCATE TABLE " + Qualified(engine, scope, target.name) + ";");
        }
        out.warnings.push_back("清空表会删除全部行，且不可恢复");
        return true;
    }

    DdlTableDefinition working = updated;
    working.engine = engine;
    if (mode == DdlMode::Create) {
        working.exists = false;
        if (!ValidateDefinition(working, error)) return false;
        out.affected_rows = "0";
        return AppendCreate(engine, working, out, error);
    }

    // Alter
    if (!ValidateDefinition(working, error)) return false;
    if (!new_name.empty()) working.name = new_name;
    if (original.name.empty()) {
        error = "缺少原始表定义，无法生成 ALTER";
        return false;
    }
    return AppendAlter(engine, scope, original, working, out, error);
}

std::string DdlPlanToken(const DdlPlan& plan) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto& statement : plan.statements) {
        for (unsigned char ch : statement) {
            hash ^= static_cast<std::uint64_t>(ch);
            hash *= 1099511628211ULL;
        }
        hash ^= 0x1F;
        hash *= 1099511628211ULL;
    }
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%016llx",
                  static_cast<unsigned long long>(hash));
    return std::string(buffer);
}

bool DdlTransactional(Engine engine, DdlMode mode) {
    if (engine == Engine::MySql) return false;
    if (mode == DdlMode::CreateDatabase || mode == DdlMode::DropDatabase) return false;
    return true;
}

}  // namespace silkit::db
