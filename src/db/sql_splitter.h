#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace silkit::db {

struct SqlStatement {
    std::string text;
    std::size_t begin_offset = 0;
    std::size_t end_offset = 0;
};

struct SplitOptions {
    bool backslash_escapes = false;  // MySQL 默认开启
    bool hash_comments = false;      // MySQL `# 注释`
    bool dollar_quotes = false;      // PostgreSQL $tag$ ... $tag$
    bool delimiter_command = false;  // MySQL DELIMITER 指令
    std::string delimiter = ";";
};

/// 把脚本拆成语句，保留每条语句在原文本中的偏移，供错误定位使用。
/// 引号/注释/dollar-quoted 内的分隔符不会切分。
std::vector<SqlStatement> SplitSql(const std::string& sql, const SplitOptions& options = {});

SplitOptions SplitOptionsForEngine(int engine_kind);

}  // namespace silkit::db
