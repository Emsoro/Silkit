#include "io/csv.h"

#include <array>
#include <utility>

namespace silkit::io {

CsvParser::CsvParser(std::string_view text, CsvOptions options)
    : text_(text), options_(options) {
    // 跳过 UTF-8 BOM：调用方通常已解码，这里兜底。
    if (text_.size() >= 3 && static_cast<unsigned char>(text_[0]) == 0xEF &&
        static_cast<unsigned char>(text_[1]) == 0xBB &&
        static_cast<unsigned char>(text_[2]) == 0xBF) {
        pos_ = 3;
    }
}

bool CsvParser::Next(std::vector<std::string>& fields) {
    fields.clear();
    if (failed_ || AtEnd()) return false;

    record_line_ = line_;
    ++record_index_;

    std::string field;
    bool in_quotes = false;
    bool field_started = false;

    for (;;) {
        if (AtEnd()) {
            if (in_quotes) {
                failed_ = true;
                error_ = "第 " + std::to_string(record_line_) + " 行开始的引号未闭合";
                return false;
            }
            fields.push_back(std::move(field));
            return true;
        }

        const char c = text_[pos_];
        if (in_quotes) {
            if (c == options_.quote) {
                if (pos_ + 1 < text_.size() && text_[pos_ + 1] == options_.quote) {
                    field.push_back(options_.quote);
                    pos_ += 2;
                } else {
                    in_quotes = false;
                    ++pos_;
                }
            } else {
                if (c == '\n') ++line_;
                field.push_back(c);
                ++pos_;
            }
            continue;
        }

        if (c == options_.quote && field.empty() && !field_started) {
            in_quotes = true;
            field_started = true;
            ++pos_;
            continue;
        }
        if (c == options_.delimiter) {
            fields.push_back(std::move(field));
            field.clear();
            field_started = false;
            ++pos_;
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (c == '\r') {
                ++pos_;
                if (!AtEnd() && text_[pos_] == '\n') ++pos_;
            } else {
                ++pos_;
            }
            ++line_;
            fields.push_back(std::move(field));
            return true;
        }

        field.push_back(c);
        field_started = true;
        ++pos_;
    }
}

std::string CsvEscapeField(std::string_view field, CsvOptions options) {
    const bool needs_quote =
        field.find(options.delimiter) != std::string_view::npos ||
        field.find(options.quote) != std::string_view::npos ||
        field.find('\r') != std::string_view::npos ||
        field.find('\n') != std::string_view::npos;
    if (!needs_quote) return std::string(field);

    std::string out;
    out.reserve(field.size() + 4);
    out.push_back(options.quote);
    for (char c : field) {
        if (c == options.quote) out.push_back(options.quote);
        out.push_back(c);
    }
    out.push_back(options.quote);
    return out;
}

void WriteCsvRow(std::string& out, const std::vector<std::string>& fields, CsvOptions options) {
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) out.push_back(options.delimiter);
        out += CsvEscapeField(fields[i], options);
    }
    out += "\r\n";
}

char DetectDelimiter(std::string_view text, char fallback) {
    constexpr std::array<char, 4> candidates{',', ';', '\t', '|'};
    std::array<std::size_t, 4> counts{0, 0, 0, 0};

    // 只看前 32 行，且忽略引号内的候选字符。
    std::size_t line = 0;
    bool in_quotes = false;
    std::size_t pos = 0;
    while (pos < text.size() && line < 32) {
        const char c = text[pos];
        if (in_quotes) {
            if (c == '"') {
                if (pos + 1 < text.size() && text[pos + 1] == '"') {
                    pos += 2;
                    continue;
                }
                in_quotes = false;
            }
            ++pos;
            continue;
        }
        if (c == '"') {
            in_quotes = true;
            ++pos;
            continue;
        }
        if (c == '\r' || c == '\n') {
            ++line;
            if (c == '\r' && pos + 1 < text.size() && text[pos + 1] == '\n') ++pos;
            ++pos;
            continue;
        }
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (c == candidates[i]) {
                ++counts[i];
                break;
            }
        }
        ++pos;
    }

    std::size_t best = 0;
    for (std::size_t i = 1; i < candidates.size(); ++i) {
        if (counts[i] > counts[best]) best = i;
    }
    return counts[best] == 0 ? fallback : candidates[best];
}

}  // namespace silkit::io
