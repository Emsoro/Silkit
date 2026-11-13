#include "db/types.h"

namespace silkit::db {

const char* EngineName(Engine engine) {
    switch (engine) {
        case Engine::Sqlite:
            return "sqlite";
        case Engine::MySql:
            return "mysql";
        case Engine::Postgres:
            return "postgres";
    }
    return "sqlite";
}

Engine EngineFromName(const std::string& name, bool& ok) {
    ok = true;
    if (name == "sqlite" || name == "sqlite3") return Engine::Sqlite;
    if (name == "mysql" || name == "mariadb") return Engine::MySql;
    if (name == "postgres" || name == "postgresql" || name == "pg") return Engine::Postgres;
    ok = false;
    return Engine::Sqlite;
}

const char* KindName(ValueKind kind) {
    switch (kind) {
        case ValueKind::Null:
            return "null";
        case ValueKind::Int:
            return "int";
        case ValueKind::UInt:
            return "uint";
        case ValueKind::Float:
            return "float";
        case ValueKind::Decimal:
            return "decimal";
        case ValueKind::Bool:
            return "bool";
        case ValueKind::Text:
            return "text";
        case ValueKind::Binary:
            return "binary";
        case ValueKind::Date:
            return "date";
        case ValueKind::Time:
            return "time";
        case ValueKind::DateTime:
            return "datetime";
        case ValueKind::Timestamp:
            return "timestamp";
        case ValueKind::Json:
            return "json";
        case ValueKind::Uuid:
            return "uuid";
        case ValueKind::Array:
            return "array";
        case ValueKind::Unknown:
            return "unknown";
    }
    return "unknown";
}

}  // namespace silkit::db
