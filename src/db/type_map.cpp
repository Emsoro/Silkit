#include "db/type_map.h"

#include <algorithm>
#include <cctype>

namespace silkit::db {
namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool Contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

ValueKind KindFromDeclType(const std::string& decl_type) {
    const std::string type = Lower(decl_type);
    if (type.empty()) return ValueKind::Unknown;

    if (Contains(type, "bool")) return ValueKind::Bool;
    if (Contains(type, "json")) return ValueKind::Json;
    if (Contains(type, "uuid") || Contains(type, "guid")) return ValueKind::Uuid;
    if (Contains(type, "timestamp") || Contains(type, "timestamptz")) return ValueKind::Timestamp;
    if (Contains(type, "datetime")) return ValueKind::DateTime;
    if (Contains(type, "date")) return ValueKind::Date;
    if (Contains(type, "time")) return ValueKind::Time;

    if (Contains(type, "unsigned") || Contains(type, "uint")) return ValueKind::UInt;
    if (Contains(type, "int") || Contains(type, "serial")) return ValueKind::Int;
    if (Contains(type, "decimal") || Contains(type, "numeric") || Contains(type, "money")) {
        return ValueKind::Decimal;
    }
    if (Contains(type, "real") || Contains(type, "floa") || Contains(type, "doub")) {
        return ValueKind::Float;
    }
    if (Contains(type, "blob") || Contains(type, "binary") || Contains(type, "bytea")) {
        return ValueKind::Binary;
    }
    if (Contains(type, "char") || Contains(type, "text") || Contains(type, "clob") ||
        Contains(type, "enum") || Contains(type, "set")) {
        return ValueKind::Text;
    }
    if (type.find('[') != std::string::npos) return ValueKind::Array;
    return ValueKind::Unknown;
}

ValueKind KindFromSqliteType(int sqlite_type, const std::string& decl_type) {
    ValueKind declared = KindFromDeclType(decl_type);
    switch (sqlite_type) {
        case 1:  // SQLITE_INTEGER
            if (declared == ValueKind::Bool || declared == ValueKind::Timestamp ||
                declared == ValueKind::DateTime || declared == ValueKind::Date ||
                declared == ValueKind::Time || declared == ValueKind::Uuid) {
                return declared;
            }
            return ValueKind::Int;
        case 2:  // SQLITE_FLOAT
            return ValueKind::Float;
        case 3:  // SQLITE_TEXT
            if (declared == ValueKind::Json || declared == ValueKind::Date ||
                declared == ValueKind::Time || declared == ValueKind::DateTime ||
                declared == ValueKind::Timestamp || declared == ValueKind::Uuid ||
                declared == ValueKind::Bool) {
                return declared;
            }
            return ValueKind::Text;
        case 4:  // SQLITE_BLOB
            return ValueKind::Binary;
        case 5:  // SQLITE_NULL
            return ValueKind::Null;
        default:
            return declared;
    }
}

}  // namespace silkit::db
