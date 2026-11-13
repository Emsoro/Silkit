#pragma once

#include "db/driver.h"

namespace silkit::db {

class MySqlDriver final : public Driver {
public:
    Engine EngineKind() const override { return Engine::MySql; }
    std::unique_ptr<Connection> Open(const ConnectParams& params, DbError& err) override;
};

}  // namespace silkit::db
