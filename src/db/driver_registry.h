#pragma once

#include "db/driver.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace silkit::db {

/// 驱动注册表：UI/IPC 只依赖 Engine，具体驱动在启动时注册。
class DriverRegistry {
public:
    static DriverRegistry& Instance();

    void Register(std::unique_ptr<Driver> driver);
    Driver* Find(Engine engine) const;

    DriverRegistry(const DriverRegistry&) = delete;
    DriverRegistry& operator=(const DriverRegistry&) = delete;

private:
    DriverRegistry() = default;

    mutable std::mutex mutex_;
    std::unordered_map<Engine, std::unique_ptr<Driver>> drivers_;
};

/// 幂等注册内置驱动（SQLite；MySQL/PostgreSQL 在后续里程碑加入）。
void RegisterBuiltinDrivers();

}  // namespace silkit::db
