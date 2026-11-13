#include "db/driver_registry.h"

#include "core/logger.h"
#include "db/drivers/sqlite_driver.h"

namespace silkit::db {

DriverRegistry& DriverRegistry::Instance() {
    static DriverRegistry registry;
    return registry;
}

void DriverRegistry::Register(std::unique_ptr<Driver> driver) {
    if (!driver) return;
    const Engine engine = driver->EngineKind();
    std::lock_guard<std::mutex> lock(mutex_);
    drivers_[engine] = std::move(driver);
}

Driver* DriverRegistry::Find(Engine engine) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = drivers_.find(engine);
    return it == drivers_.end() ? nullptr : it->second.get();
}

void RegisterBuiltinDrivers() {
    static std::once_flag once;
    std::call_once(once, [] {
        DriverRegistry::Instance().Register(std::make_unique<SqliteDriver>());
        LogInfo("db: 已注册内置驱动 sqlite");
    });
}

}  // namespace silkit::db
