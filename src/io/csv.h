#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace silkit::io {

/// M7-1：RFC4180 CSV 解析/写出。分隔符可自定义，支持引号转义与跨行字段。
struct CsvOptions {
    char delimiter = ',';
    char quote = '"';
};

/// 逐条读取 CSV 记录。每次 Next 返回一条记录（字段已去引号）。
class CsvParser {
public:
    CsvParser(std::string_view text, CsvOptions options = {});

    /// 返回 false 表示已到结尾或解析失败（用 failed()/error() 区分）。
    bool Next(std::vector<std::string>& fields);

    bool failed() const { return failed_; }
    const std::string& error() const { return error_; }

    /// 最近一条记录在原文中的起始行号（1-based，含引号内换行）。
    std::size_t line() const { return record_line_; }
    /// 最近一条记录的序号（1-based）。
    std::size_t record() const { return record_index_; }

private:
    bool AtEnd() const { return pos_ >= text_.size(); }

    std::string_view text_;
    CsvOptions options_;
    std::size_t pos_ = 0;
    std::size_t line_ = 1;
    std::size_t record_line_ = 1;
    std::size_t record_index_ = 0;
    bool failed_ = false;
    std::string error_;
};

/// 按需转义单个字段（不含分隔符）。
std::string CsvEscapeField(std::string_view field, CsvOptions options = {});

/// 把一条记录追加到 out，行尾使用 CRLF。
void WriteCsvRow(std::string& out, const std::vector<std::string>& fields,
                 CsvOptions options = {});

/// 从文本前若干行探测分隔符（逗号/分号/制表符/竖线），无结果时返回 fallback。
char DetectDelimiter(std::string_view text, char fallback = ',');

}  // namespace silkit::io
