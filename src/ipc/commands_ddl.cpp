#include "ipc/commands_ddl.h"

#include "db/ddl_builder.h"
#include "db/engine_dialect.h"
#include "db/identifier.h"
#include "db/session.h"
#include "db/types.h"
#include "ipc/commands_db_common.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace silkit::ipc {
namespace {

using dbutil::json;

std::string Text(const json& object, const char* key, const std::string& fallback = {}) {
    if (!object.is_object() || !object.contains(key) || object[key].is_null()) return fallback;
    const auto& value = object[key];
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number_integer()) return std::to_string(value.get<long long>());
    if (value.is_number_unsigned()) return std::to_string(value.get<unsigned long long>());
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    return fallback;
}

bool Flag(const json& object, const char* key, bool fallback = false) {
    if (!object.is_object() || !object.contains(key) || object[key].is_null()) return fallback;
    const auto& value = object[key];
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number_integer()) return value.get<long long>() != 0;
    if (value.is_string()) return value.get<std::string>() == "true" || value.get<std::string>() == "1";
    return fallback;
}

std::vector<std::string> StringArray(const json& value) {
    std::vector<std::string> out;
    if (!value.is_array()) return out;
    for (const auto& item : value) {
        if (item.is_string()) out.push_back(item.get<std::string>());
    }
    return out;
}

db::DdlColumn ColumnFromJson(const json& value) {
    db::DdlColumn column;
    column.name = Text(value, "name");
    column.type = Text(value, "type");
    column.not_null = Flag(value, "notNull");
    column.primary_key = Flag(value, "primaryKey");
    column.auto_increment = Flag(value, "autoIncrement");
    column.default_expr = Text(value, "default");
    column.has_default = Flag(value, "hasDefault", !column.default_expr.empty());
    column.comment = Text(value, "comment");
    column.old_name = Text(value, "oldName");
    return column;
}

db::DdlIndex IndexFromJson(const json& value) {
    db::DdlIndex index;
    index.name = Text(value, "name");
    index.unique = Flag(value, "unique");
    index.columns = StringArray(value.value("columns", json::array()));
    return index;
}

db::DdlForeignKey ForeignKeyFromJson(const json& value) {
    db::DdlForeignKey fk;
    fk.name = Text(value, "name");
    fk.columns = StringArray(value.value("columns", json::array()));
    fk.ref_table = Text(value, "refTable");
    fk.ref_schema = Text(value, "refSchema");
    fk.ref_columns = StringArray(value.value("refColumns", json::array()));
    fk.on_update = Text(value, "onUpdate", "NO ACTION");
    fk.on_delete = Text(value, "onDelete", "NO ACTION");
    return fk;
}

db::DdlTableDefinition DefinitionFromJson(db::Engine engine, const json& value) {
    db::DdlTableDefinition definition;
    definition.engine = engine;
    definition.scope = Text(value, "scope");
    definition.name = Text(value, "name");
    definition.primary_key_name = Text(value, "primaryKeyName");
    definition.exists = Flag(value, "exists");
    if (value.contains("columns") && value["columns"].is_array()) {
        for (const auto& item : value["columns"]) {
            definition.columns.push_back(ColumnFromJson(item));
        }
    }
    if (value.contains("indexes") && value["indexes"].is_array()) {
        for (const auto& item : value["indexes"]) definition.indexes.push_back(IndexFromJson(item));
    }
    if (value.contains("foreignKeys") && value["foreignKeys"].is_array()) {
        for (const auto& item : value["foreignKeys"]) {
            definition.foreign_keys.push_back(ForeignKeyFromJson(item));
        }
    }
    if (value.contains("options") && value["options"].is_object()) {
        const auto& options = value["options"];
        definition.options.engine = Text(options, "engine");
        definition.options.charset = Text(options, "charset");
        definition.options.collation = Text(options, "collation");
        definition.options.comment = Text(options, "comment");
    }
    return definition;
}

json ColumnJson(const db::DdlColumn& column) {
    return json{{"name", column.name},
                {"type", column.type},
                {"notNull", column.not_null},
                {"primaryKey", column.primary_key},
                {"autoIncrement", column.auto_increment},
                {"hasDefault", column.has_default},
                {"default", column.default_expr},
                {"comment", column.comment},
                {"oldName", column.old_name}};
}

json DefinitionJson(const db::DdlTableDefinition& definition) {
    json columns = json::array();
    for (const auto& column : definition.columns) columns.push_back(ColumnJson(column));

    json indexes = json::array();
    for (const auto& index : definition.indexes) {
        indexes.push_back(
            json{{"name", index.name}, {"unique", index.unique}, {"columns", index.columns}});
    }

    json foreign_keys = json::array();
    for (const auto& fk : definition.foreign_keys) {
        foreign_keys.push_back(json{{"name", fk.name},
                                    {"columns", fk.columns},
                                    {"refTable", fk.ref_table},
                                    {"refSchema", fk.ref_schema},
                                    {"refColumns", fk.ref_columns},
                                    {"onUpdate", fk.on_update},
                                    {"onDelete", fk.on_delete}});
    }

    return json{{"engine", db::EngineName(definition.engine)},
                {"scope", definition.scope},
                {"name", definition.name},
                {"exists", definition.exists},
                {"primaryKeyName", definition.primary_key_name},
                {"columns", std::move(columns)},
                {"indexes", std::move(indexes)},
                {"foreignKeys", std::move(foreign_keys)},
                {"options", json{{"engine", definition.options.engine},
                                 {"charset", definition.options.charset},
                                 {"collation", definition.options.collation},
                                 {"comment", definition.options.comment}}}};
}

std::string ScopeFrom(session::Session& session, const json& ref, const json& definition) {
    const db::Engine engine = session.Conn().EngineKind();
    std::string scope = Text(definition, "scope");
    if (scope.empty()) scope = Text(ref, "database");
    if (scope.empty()) scope = Text(ref, "schema");
    if (scope.empty()) {
        if (engine == db::Engine::MySql) scope = session.CurrentDatabase();
        if (engine == db::Engine::Postgres) scope = "public";
        if (engine == db::Engine::Sqlite) scope = "main";
    }
    return scope;
}

struct PreparedPlan {
    db::DdlMode mode = db::DdlMode::Create;
    db::DdlTableDefinition original;
    db::DdlTableDefinition updated;
    db::DdlPlan plan;
    std::string scope;
    db::Engine engine = db::Engine::Sqlite;
    bool transactional = false;
};

PreparedPlan PreparePlan(session::Session& session, const json& args) {
    PreparedPlan prepared;
    db::Connection& connection = session.Conn();
    prepared.engine = connection.EngineKind();

    const std::string mode_name = Text(args, "mode", "create");
    bool mode_ok = false;
    prepared.mode = db::DdlModeFromName(mode_name, mode_ok);
    if (!mode_ok) dbutil::ThrowCommandError("bad_args", "不支持的 DDL 模式: " + mode_name);

    const json ref = args.value("ref", json::object());
    const json definition_json = args.value("definition", json::object());
    prepared.scope = ScopeFrom(session, ref, definition_json);
    if (prepared.scope.empty()) {
        dbutil::ThrowCommandError("bad_args", "缺少数据库/Schema");
    }

    prepared.updated = DefinitionFromJson(prepared.engine, definition_json);
    prepared.updated.scope = prepared.scope;
    if (prepared.updated.name.empty()) prepared.updated.name = Text(ref, "name");
    const std::string new_name = Text(args, "newName");

    std::string error;
    switch (prepared.mode) {
        case db::DdlMode::Create:
            if (!db::BuildDdlPlan(prepared.mode, prepared.engine, prepared.scope, {},
                                  prepared.updated, new_name, prepared.plan, error)) {
                dbutil::ThrowCommandError("bad_args", error);
            }
            break;
        case db::DdlMode::Alter: {
            const std::string original_name =
                Text(ref, "name", prepared.updated.name);
            if (original_name.empty()) {
                dbutil::ThrowCommandError("bad_args", "缺少表名");
            }
            db::DbError load_error;
            if (!db::LoadDdlDefinition(connection, prepared.scope, original_name, prepared.original,
                                       load_error)) {
                throw IpcError(dbutil::ErrorJson(load_error));
            }
            if (!db::BuildDdlPlan(prepared.mode, prepared.engine, prepared.scope,
                                  prepared.original, prepared.updated, new_name, prepared.plan,
                                  error)) {
                dbutil::ThrowCommandError("bad_args", error);
            }
            break;
        }
        case db::DdlMode::Drop:
        case db::DdlMode::Truncate:
            if (prepared.updated.name.empty()) {
                dbutil::ThrowCommandError("bad_args", "缺少表名");
            }
            if (!db::BuildDdlPlan(prepared.mode, prepared.engine, prepared.scope, {},
                                  prepared.updated, new_name, prepared.plan, error)) {
                dbutil::ThrowCommandError("bad_args", error);
            }
            break;
        case db::DdlMode::CreateDatabase:
        case db::DdlMode::DropDatabase:
            if (prepared.updated.name.empty()) prepared.updated.name = Text(ref, "database");
            if (!db::BuildDdlPlan(prepared.mode, prepared.engine, prepared.scope, {},
                                  prepared.updated, new_name, prepared.plan, error)) {
                dbutil::ThrowCommandError("bad_args", error);
            }
            break;
    }

    prepared.transactional = db::DdlTransactional(prepared.engine, prepared.mode);
    return prepared;
}

json PlanJson(const PreparedPlan& prepared, const json& args) {
    json statements = json::array();
    std::string sql;
    for (const auto& statement : prepared.plan.statements) {
        statements.push_back(statement);
        if (!sql.empty()) sql += "\n\n";
        sql += statement;
    }
    json warnings = json::array();
    for (const auto& warning : prepared.plan.warnings) warnings.push_back(warning);

    return json{{"engine", db::EngineName(prepared.engine)},
                {"mode", Text(args, "mode", "create")},
                {"scope", prepared.scope},
                {"statements", std::move(statements)},
                {"sql", sql},
                {"token", db::DdlPlanToken(prepared.plan)},
                {"warnings", std::move(warnings)},
                {"transactional", prepared.transactional}};
}

void CheckApplyConfirmation(const PreparedPlan& prepared, const json& args) {
    const std::string token = Text(args, "token");
    if (token.empty()) {
        dbutil::ThrowCommandError("bad_args", "请先预览 SQL，再执行变更");
    }
    const std::string expected = db::DdlPlanToken(prepared.plan);
    if (token != expected) {
        dbutil::ThrowCommandError("bad_args", "DDL 已变化，请重新预览后再执行");
    }

    const std::string confirm = Text(args, "confirmName");
    if (prepared.mode == db::DdlMode::Drop || prepared.mode == db::DdlMode::Truncate) {
        if (confirm != prepared.updated.name) {
            dbutil::ThrowCommandError("bad_args", "请输入完整表名确认操作");
        }
    }
    if (prepared.mode == db::DdlMode::DropDatabase) {
        std::string name = Text(args, "newName");
        if (name.empty()) name = prepared.updated.name;
        if (confirm != name) {
            dbutil::ThrowCommandError("bad_args", "请输入完整数据库名确认删除操作");
        }
    }
}

db::ExecResult Exec(db::Connection& connection, const std::string& sql) {
    return dbutil::RunRows(connection, sql).result;
}

json ApplyPlan(session::Session& sess, const PreparedPlan& prepared, const json& args) {
    if (sess.IsReadOnly()) {
        dbutil::ThrowCommandError("read_only", "只读连接不允许执行 DDL");
    }
    CheckApplyConfirmation(prepared, args);
    if (prepared.plan.statements.empty()) {
        dbutil::ThrowCommandError("bad_args", "没有需要执行的语句");
    }

    if (Flag(args, "dryRun")) {
        json output = PlanJson(prepared, args);
        output["ok"] = true;
        output["dryRun"] = true;
        return output;
    }

    db::Connection& connection = sess.Conn();
    const bool use_transaction =
        prepared.transactional && sess.Tx() == session::TxState::Autocommit;
    const bool use_savepoint =
        prepared.transactional && sess.Tx() == session::TxState::InTransaction;

    if (use_transaction) {
        auto begin = Exec(connection, "BEGIN");
        if (begin.error.code != 0) throw IpcError(dbutil::ErrorJson(begin.error));
        sess.SetTx(session::TxState::InTransaction);
    } else if (use_savepoint) {
        auto savepoint = Exec(connection, "SAVEPOINT silkit_ddl");
        if (savepoint.error.code != 0) throw IpcError(dbutil::ErrorJson(savepoint.error));
    }

    const auto started = std::chrono::steady_clock::now();
    std::size_t executed = 0;
    for (std::size_t i = 0; i < prepared.plan.statements.size(); ++i) {
        const std::string& statement = prepared.plan.statements[i];
        auto result = Exec(connection, statement);
        if (result.error.code != 0) {
            bool rolled_back = false;
            if (use_transaction) {
                Exec(connection, "ROLLBACK");
                sess.SetTx(session::TxState::Autocommit);
                rolled_back = true;
            } else if (use_savepoint) {
                Exec(connection, "ROLLBACK TO SAVEPOINT silkit_ddl");
                Exec(connection, "RELEASE SAVEPOINT silkit_ddl");
                rolled_back = true;
            }
            json error = dbutil::ErrorJson(result.error);
            error["code"] = "ddl_failed";
            error["dbCode"] = result.error.code;
            error["statementIndex"] = i;
            error["statement"] = statement;
            error["applied"] = executed;
            error["rolledBack"] = rolled_back;
            error["transactional"] = prepared.transactional;
            throw IpcError(std::move(error));
        }
        ++executed;
    }

    if (use_transaction) {
        auto commit = Exec(connection, "COMMIT");
        if (commit.error.code != 0) {
            Exec(connection, "ROLLBACK");
            sess.SetTx(session::TxState::Autocommit);
            throw IpcError(dbutil::ErrorJson(commit.error));
        }
        sess.SetTx(session::TxState::Autocommit);
    } else if (use_savepoint) {
        Exec(connection, "RELEASE SAVEPOINT silkit_ddl");
    }

    sess.InvalidateMetadata();
    const auto elapsed = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started)
            .count());

    json warnings = json::array();
    for (const auto& warning : prepared.plan.warnings) warnings.push_back(warning);
    if (!prepared.transactional) {
        warnings.push_back("MySQL DDL 会隐式提交，失败后无法自动回滚");
    }

    json executed_statements = json::array();
    for (const auto& statement : prepared.plan.statements) executed_statements.push_back(statement);

    return json{{"ok", true},
                {"applied", executed},
                {"statements", std::move(executed_statements)},
                {"warnings", std::move(warnings)},
                {"transactional", prepared.transactional},
                {"rolledBack", false},
                {"elapsedMs", elapsed}};
}

}  // namespace

void RegisterDdlCommands(AsyncRouter& router, session::SessionManager& sessions) {
    router.Register("ddl.types", [&sessions](const json& args, TaskContext&) -> json {
        db::Engine engine = db::Engine::Sqlite;
        const std::string engine_name = Text(args, "engine");
        if (!engine_name.empty()) {
            bool ok = false;
            engine = db::EngineFromName(engine_name, ok);
            if (!ok) dbutil::ThrowCommandError("bad_args", "不支持的数据库引擎: " + engine_name);
        } else {
            auto session = dbutil::RequireSession(sessions, Text(args, "sessionId"));
            engine = session->RunSync([&session]() -> db::Engine {
                return session->Conn().EngineKind();
            });
        }

        json types = json::array();
        for (const auto& type : db::DdlTypeSuggestions(engine)) types.push_back(type);
        const bool mysql = engine == db::Engine::MySql;
        const bool not_sqlite = engine != db::Engine::Sqlite;

        return json{
            {"engine", db::EngineName(engine)},
            {"types", std::move(types)},
            {"supports",
             json{{"columnComment", not_sqlite},
                  {"tableComment", not_sqlite},
                  {"storageOptions", mysql},
                  {"alterColumn", not_sqlite},
                  {"alterForeignKey", not_sqlite},
                  {"alterPrimaryKey", not_sqlite},
                  {"transactionalDdl", !mysql},
                  {"database", not_sqlite}}}};
    });

    router.Register("ddl.load", [&sessions](const json& args, TaskContext&) -> json {
        auto session = dbutil::RequireSession(sessions, Text(args, "sessionId"));
        const json ref = args.value("ref", json::object());
        const std::string name = Text(ref, "name");
        if (name.empty()) dbutil::ThrowCommandError("bad_args", "缺少表名");
        if (Text(ref, "kind", "table") != "table") {
            dbutil::ThrowCommandError("bad_args", "设计器仅支持表对象");
        }

        return session->RunSync([&]() -> json {
            const std::string scope = ScopeFrom(*session, ref, json::object());
            if (scope.empty()) dbutil::ThrowCommandError("bad_args", "缺少数据库/Schema");
            db::DdlTableDefinition definition;
            db::DbError error;
            if (!db::LoadDdlDefinition(session->Conn(), scope, name, definition, error)) {
                throw IpcError(dbutil::ErrorJson(error));
            }
            return json{{"definition", DefinitionJson(definition)}, {"warnings", json::array()}};
        });
    });

    router.Register("ddl.tables", [&sessions](const json& args, TaskContext&) -> json {
        auto session = dbutil::RequireSession(sessions, Text(args, "sessionId"));
        const json ref = args.value("ref", json::object());
        return session->RunSync([&]() -> json {
            const std::string scope = ScopeFrom(*session, ref, json::object());
            if (scope.empty()) dbutil::ThrowCommandError("bad_args", "缺少数据库/Schema");
            const db::Engine engine = session->Conn().EngineKind();
            auto rows = dbutil::RunRows(session->Conn(),
                                        db::ListObjectNamesSql(engine, scope, "tables"));
            if (!rows.ok()) throw IpcError(dbutil::ErrorJson(rows.result.error));
            json tables = json::array();
            for (const auto& row : rows.rows) {
                if (!row.empty() && !row[0].is_null && !row[0].text.empty()) {
                    tables.push_back(row[0].text);
                }
            }
            return json{{"tables", std::move(tables)}};
        });
    });

    router.Register("ddl.preview", [&sessions](const json& args, TaskContext&) -> json {
        auto session = dbutil::RequireSession(sessions, Text(args, "sessionId"));
        return session->RunSync([&]() -> json {
            const PreparedPlan prepared = PreparePlan(*session, args);
            return PlanJson(prepared, args);
        });
    });

    router.Register("ddl.apply", [&sessions](const json& args, TaskContext&) -> json {
        auto session = dbutil::RequireSession(sessions, Text(args, "sessionId"));
        return session->RunSync([&]() -> json {
            const PreparedPlan prepared = PreparePlan(*session, args);
            return ApplyPlan(*session, prepared, args);
        });
    });
}

}  // namespace silkit::ipc
