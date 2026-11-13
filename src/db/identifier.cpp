#include "db/identifier.h"

#include <cctype>

namespace silkit::db {
namespace {

std::string EscapeIdentifier(const std::string& name, char quote) {
    std::string out;
    out.reserve(name.size() + 2);
    out.push_back(quote);
    for (char ch : name) {
        if (ch == quote) out.push_back(quote);
        out.push_back(ch);
    }
    out.push_back(quote);
    return out;
}

}  // namespace

std::string QuoteIdentifier(Engine engine, const std::string& name) {
    const char quote = engine == Engine::MySql ? '`' : '"';
    return EscapeIdentifier(name, quote);
}

std::string QuoteStringLiteral(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('\'');
    for (char ch : value) {
        if (ch == '\'') out.push_back('\'');
        out.push_back(ch);
    }
    out.push_back('\'');
    return out;
}

bool IsSafeIdentifier(const std::string& name) {
    if (name.empty() || name.size() > 255) return false;
    for (unsigned char ch : name) {
        if (ch < 0x20 || ch == 0x7F) return false;
    }
    return true;
}

}  // namespace silkit::db
