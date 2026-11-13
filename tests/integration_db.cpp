// M2/M3 集成冒烟：仅在设置 DSN 环境变量时连接真实服务器；未设置时跳过。
// MySQL:   SILKIT_MYSQL_HOST/PORT/USER/PASSWORD/DATABASE
// MariaDB: 同一组变量（libmariadb）
// PG:      SILKIT_PG_HOST/PORT/USER/PASSWORD/DATABASE/SCHEMA
#include "db/driver_registry.h"
#include "db/types.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int g_failed = 0;

std::string Env(const char* key, const std::string& fallback = {}) {
    const char* value = std::getenv(key);
    return value ? value : fallback;
}

void Probe(silkit::db::Engine engine, const char* prefix, const char* default_port,
           const char* default_user, const char* default_database, const char* label) {
    const std::string host_key = std::string(prefix) + "_HOST";
    const std::string host = Env(host_key.c_str());
    if (host.empty()) {
        std::printf("  [SKIP] %s：未设置 %s\n", label, host_key.c_str());
        return;
    }
    silkit::db::ConnectParams params;
    params.engine = engine;
    params.host = host;
    params.port = Env((std::string(prefix) + "_PORT").c_str(), default_port);
    params.user = Env((std::string(prefix) + "_USER").c_str(), default_user);
    params.password = Env((std::string(prefix) + "_PASSWORD").c_str());
    params.database = Env((std::string(prefix) + "_DATABASE").c_str(), default_database);
    params.schema = Env((std::string(prefix) + "_SCHEMA").c_str());
    params.connect_timeout_ms = 5000;

    silkit::db::Driver* driver = silkit::db::DriverRegistry::Instance().Find(engine);
    if (!driver) {
        ++g_failed;
        std::printf("  [FAIL] %s：驱动未注册\n", label);
        return;
    }
    silkit::db::DbError error;
    auto connection = driver->Open(params, error);
    if (!connection) {
        ++g_failed;
        std::printf("  [FAIL] %s 连接失败：%s (%s)\n", label, error.message.c_str(),
                    error.sql_state.c_str());
        return;
    }

    silkit::db::DbError ping_error;
    if (!connection->Ping(ping_error)) {
        ++g_failed;
        std::printf("  [FAIL] %s ping：%s\n", label, ping_error.message.c_str());
    }

    std::uint64_t seen = 0;
    silkit::db::ExecOptions options;
    options.fetch_size = 10;
    const silkit::db::ExecResult result = connection->Execute(
        "SELECT 1 AS one", {}, options, [&seen](const silkit::db::RowBatch& batch) {
            seen += batch.rows.size();
            return true;
        });
    if (result.error.code != 0 || seen == 0) {
        ++g_failed;
        std::printf("  [FAIL] %s SELECT 1：code=%d rows=%llu\n", label,
                    result.error.code, static_cast<unsigned long long>(seen));
    } else {
        std::printf("  [OK]   %s v%s · thread=%llu · db=%s · placeholder=%s\n",
                    label, connection->ServerVersion().c_str(),
                    static_cast<unsigned long long>(connection->ThreadId()),
                    connection->CurrentDatabase().c_str(),
                    connection->Placeholder(1).c_str());
    }
}

}  // namespace

int main() {
    std::printf("Silkit M2/M3 integration smoke\n");
    silkit::db::RegisterBuiltinDrivers();
    Probe(silkit::db::Engine::MySql, "SILKIT_MYSQL", "3306", "root", "", "MySQL/MariaDB");
    Probe(silkit::db::Engine::Postgres, "SILKIT_PG", "5432", "postgres", "postgres", "PostgreSQL");
    std::printf("%d failures\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
