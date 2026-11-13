#include "db/sql_splitter.h"

#include <cctype>

namespace silkit::db {
namespace {

bool IsSpace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v';
}

char LowerAscii(char ch) {
    if (ch >= 'A' && ch <= 'Z') return static_cast<char>(ch - 'A' + 'a');
    return ch;
}

bool MatchWord(const std::string& text, std::size_t offset, const char* word) {
    std::size_t i = offset;
    for (const char* p = word; *p != '\0'; ++p, ++i) {
        if (i >= text.size() || LowerAscii(text[i]) != *p) return false;
    }
    if (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_')) {
        return false;
    }
    return true;
}

bool StartsWith(const std::string& text, std::size_t offset, const std::string& needle) {
    if (needle.empty() || offset + needle.size() > text.size()) return false;
    return text.compare(offset, needle.size(), needle) == 0;
}

/// 判断区间内是否存在真正的 SQL 代码（纯空白/注释返回 false）。
bool HasCode(const std::string& sql, std::size_t begin, std::size_t end, const SplitOptions& opt) {
    std::size_t i = begin;
    while (i < end) {
        const char ch = sql[i];
        if (IsSpace(ch)) {
            ++i;
            continue;
        }
        if (ch == '-' && i + 1 < end && sql[i + 1] == '-') {
            i += 2;
            while (i < end && sql[i] != '\n') ++i;
            continue;
        }
        if (opt.hash_comments && ch == '#') {
            while (i < end && sql[i] != '\n') ++i;
            continue;
        }
        if (ch == '/' && i + 1 < end && sql[i + 1] == '*') {
            i += 2;
            while (i + 1 < end && !(sql[i] == '*' && sql[i + 1] == '/')) ++i;
            i = (i + 1 < end) ? i + 2 : end;
            continue;
        }
        return true;
    }
    return false;
}

void EmitStatement(const std::string& sql,
                   std::size_t begin,
                   std::size_t end,
                   const SplitOptions& opt,
                   std::vector<SqlStatement>& out) {
    while (begin < end && IsSpace(sql[begin])) ++begin;
    while (end > begin && IsSpace(sql[end - 1])) --end;
    if (begin >= end) return;
    if (!HasCode(sql, begin, end, opt)) return;
    out.push_back(SqlStatement{sql.substr(begin, end - begin), begin, end});
}

/// 解析 $tag$ 结束位置；不是合法的 dollar quote 返回 npos。
std::size_t SkipDollarQuoted(const std::string& sql, std::size_t offset, std::size_t end) {
    if (offset >= end || sql[offset] != '$') return std::string::npos;
    std::size_t tag_end = sql.find('$', offset + 1);
    if (tag_end == std::string::npos || tag_end >= end) return std::string::npos;
    if (tag_end == offset + 1) {
        // $$ ... $$
        const std::size_t close = sql.find("$$", tag_end + 1);
        return close == std::string::npos ? end : close + 2;
    }
    for (std::size_t i = offset + 1; i < tag_end; ++i) {
        const unsigned char ch = static_cast<unsigned char>(sql[i]);
        if (!(std::isalnum(ch) || ch == '_')) return std::string::npos;
    }
    if (std::isdigit(static_cast<unsigned char>(sql[offset + 1]))) return std::string::npos;
    const std::string tag = sql.substr(offset, tag_end - offset + 1);
    const std::size_t close = sql.find(tag, tag_end + 1);
    return close == std::string::npos ? end : close + tag.size();
}

/// 尝试识别行首的 DELIMITER 指令，成功则更新 delimiter 并返回下一行起点。
bool TryDelimiter(const std::string& sql,
                  std::size_t offset,
                  std::size_t& next_offset,
                  std::string& delimiter) {
    if (!MatchWord(sql, offset, "delimiter")) return false;
    std::size_t i = offset + 9;  // strlen("delimiter")
    if (i >= sql.size() || !IsSpace(sql[i])) return false;
    while (i < sql.size() && (sql[i] == ' ' || sql[i] == '\t')) ++i;
    const std::size_t start = i;
    while (i < sql.size() && sql[i] != '\r' && sql[i] != '\n') ++i;
    std::size_t finish = i;
    while (finish > start && IsSpace(sql[finish - 1])) --finish;
    if (finish == start) return false;
    delimiter = sql.substr(start, finish - start);
    if (i < sql.size() && sql[i] == '\r') ++i;
    if (i < sql.size() && sql[i] == '\n') ++i;
    next_offset = i;
    return true;
}

}  // namespace

std::vector<SqlStatement> SplitSql(const std::string& sql, const SplitOptions& options) {
    SplitOptions opt = options;
    if (opt.delimiter.empty()) opt.delimiter = ";";

    std::vector<SqlStatement> out;
    std::string delimiter = opt.delimiter;
    std::size_t statement_begin = 0;
    std::size_t i = 0;

    while (i < sql.size()) {
        const char ch = sql[i];

        // 行首 DELIMITER（仅当当前语句还没有代码时生效）
        if (opt.delimiter_command && (i == 0 || sql[i - 1] == '\n') &&
            (ch == 'd' || ch == 'D') && !HasCode(sql, statement_begin, i, opt)) {
            std::size_t next = i;
            std::string parsed;
            if (TryDelimiter(sql, i, next, parsed)) {
                delimiter = parsed;
                statement_begin = next;
                i = next;
                continue;
            }
        }

        if (ch == '\'' || ch == '"' || ch == '`') {
            const char quote = ch;
            ++i;
            while (i < sql.size()) {
                if (opt.backslash_escapes && sql[i] == '\\' && i + 1 < sql.size()) {
                    i += 2;
                    continue;
                }
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

        if (opt.hash_comments && ch == '#') {
            while (i < sql.size() && sql[i] != '\n') ++i;
            continue;
        }

        if (ch == '/' && i + 1 < sql.size() && sql[i + 1] == '*') {
            i += 2;
            while (i + 1 < sql.size() && !(sql[i] == '*' && sql[i + 1] == '/')) ++i;
            i = (i + 1 < sql.size()) ? i + 2 : sql.size();
            continue;
        }

        if (opt.dollar_quotes && ch == '$') {
            const std::size_t next = SkipDollarQuoted(sql, i, sql.size());
            if (next != std::string::npos) {
                i = next;
                continue;
            }
        }

        if (StartsWith(sql, i, delimiter)) {
            const std::size_t end = i + delimiter.size();
            EmitStatement(sql, statement_begin, end, opt, out);
            statement_begin = end;
            i = end;
            continue;
        }

        ++i;
    }

    EmitStatement(sql, statement_begin, sql.size(), opt, out);
    return out;
}

SplitOptions SplitOptionsForEngine(int engine_kind) {
    SplitOptions opt;
    if (engine_kind == 1) {  // mysql
        opt.backslash_escapes = true;
        opt.hash_comments = true;
        opt.delimiter_command = true;
    } else if (engine_kind == 2) {  // postgres
        opt.dollar_quotes = true;
    }
    return opt;
}

}  // namespace silkit::db
