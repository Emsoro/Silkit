#include "db/sql_util.h"

#include <algorithm>
#include <cctype>

namespace silkit::db {
namespace {

char UpperAscii(char ch) {
    if (ch >= 'a' && ch <= 'z') return static_cast<char>(ch - 'a' + 'A');
    return ch;
}

bool IsSpace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v';
}

std::string UpperCopy(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return out;
}

/// 扫描 SQL（跳过字符串/引号标识符/注释）里是否出现写关键字。
/// 用于 `WITH ... INSERT/UPDATE/DELETE` 这类以 WITH 开头的写语句，
/// 以及会修改数据库状态的 PRAGMA。
bool ContainsWriteKeyword(const std::string& sql) {
    static const char* const kWriteKeywords[] = {
        "INSERT", "UPDATE", "DELETE", "REPLACE", "CREATE", "DROP", "ALTER",
        "TRUNCATE", "VACUUM", "ATTACH", "DETACH", "REINDEX", "GRANT", "REVOKE"};

    std::size_t i = 0;
    while (i < sql.size()) {
        const char ch = sql[i];
        if (ch == '\'' || ch == '"' || ch == '`') {
            const char quote = ch;
            ++i;
            while (i < sql.size()) {
                if (sql[i] == quote) {
                    if (i + 1 < sql.size() && sql[i + 1] == quote) {
                        i += 2;
                        continue;
                    }
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }
        if (ch == '[') {
            ++i;
            while (i < sql.size() && sql[i] != ']') ++i;
            if (i < sql.size()) ++i;
            continue;
        }
        if (ch == '-' && i + 1 < sql.size() && sql[i + 1] == '-') {
            i += 2;
            while (i < sql.size() && sql[i] != '\n') ++i;
            continue;
        }
        if (ch == '/' && i + 1 < sql.size() && sql[i + 1] == '*') {
            i += 2;
            while (i + 1 < sql.size() && !(sql[i] == '*' && sql[i + 1] == '/')) ++i;
            i = (i + 1 < sql.size()) ? i + 2 : sql.size();
            continue;
        }
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
            ++i;
            continue;
        }

        std::string word;
        while (i < sql.size() &&
               (std::isalnum(static_cast<unsigned char>(sql[i])) || sql[i] == '_')) {
            word.push_back(UpperAscii(sql[i]));
            ++i;
        }
        for (const char* keyword : kWriteKeywords) {
            if (word == keyword) return true;
        }
    }
    return false;
}

}  // namespace

std::string FirstSqlKeyword(const std::string& sql) {
    std::size_t i = 0;
    while (i < sql.size()) {
        while (i < sql.size() && IsSpace(sql[i])) ++i;
        if (i + 1 < sql.size() && sql[i] == '-' && sql[i + 1] == '-') {
            i += 2;
            while (i < sql.size() && sql[i] != '\n') ++i;
            continue;
        }
        if (i + 1 < sql.size() && sql[i] == '/' && sql[i + 1] == '*') {
            i += 2;
            while (i + 1 < sql.size() && !(sql[i] == '*' && sql[i + 1] == '/')) ++i;
            i = (i + 1 < sql.size()) ? i + 2 : sql.size();
            continue;
        }
        if (sql[i] == '#') {
            while (i < sql.size() && sql[i] != '\n') ++i;
            continue;
        }
        break;
    }

    std::string word;
    while (i < sql.size() && (std::isalnum(static_cast<unsigned char>(sql[i])) || sql[i] == '_')) {
        word.push_back(UpperAscii(sql[i]));
        ++i;
    }
    return word;
}

bool IsReadOnlyStatement(const std::string& sql) {
    const std::string keyword = FirstSqlKeyword(sql);
    if (keyword == "WITH") {
        // `WITH cte AS (...) INSERT INTO ...` 是写语句，不能因为开头是 WITH 就放行。
        return !ContainsWriteKeyword(sql);
    }
    if (keyword == "PRAGMA") {
        // 带赋值或写关键字的 PRAGMA 会修改数据库状态（journal_mode / user_version 等）。
        return !ContainsWriteKeyword(sql) && UpperCopy(sql).find('=') == std::string::npos;
    }
    return keyword == "SELECT" || keyword == "EXPLAIN" || keyword == "VALUES" ||
           keyword == "SHOW" || keyword == "DESCRIBE" || keyword == "DESC";
}

bool IsDestructiveStatement(const std::string& sql) {
    const std::string keyword = FirstSqlKeyword(sql);
    if (keyword == "DROP" || keyword == "TRUNCATE" || keyword == "ALTER" ||
        keyword == "GRANT" || keyword == "REVOKE" || keyword == "ATTACH" ||
        keyword == "DETACH" || keyword == "VACUUM") {
        return true;
    }
    const std::string upper = UpperCopy(sql);
    if (keyword == "DELETE") {
        return upper.find(" WHERE ") == std::string::npos;
    }
    if (keyword == "UPDATE") {
        return upper.find(" WHERE ") == std::string::npos;
    }
    return false;
}

std::vector<DestructiveHit> FindDestructiveStatements(const std::vector<SqlStatement>& statements) {
    std::vector<DestructiveHit> hits;
    for (std::size_t i = 0; i < statements.size(); ++i) {
        if (IsDestructiveStatement(statements[i].text)) {
            hits.push_back(DestructiveHit{i, statements[i].text});
        }
    }
    return hits;
}

std::uint64_t DestructiveToken(const std::vector<SqlStatement>& statements) {
    std::uint64_t hash = 1469598103934665603ull;  // FNV-1a 64
    const auto mix = [&hash](const std::string& text) {
        for (const unsigned char ch : text) {
            hash ^= ch;
            hash *= 1099511628211ull;
        }
    };
    for (std::size_t i = 0; i < statements.size(); ++i) {
        mix(std::to_string(i));
        mix(":");
        mix(statements[i].text);
        mix("\n");
    }
    return hash;
}

}  // namespace silkit::db
