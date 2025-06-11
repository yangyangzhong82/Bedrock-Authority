/*
#pragma once

#include "db/IDatabase.h"
#include <libpq-fe.h>  // Include PostgreSQL C API
#include <string>
#include <vector>

namespace BA {
namespace db {

class PostgreSQLDatabase : public IDatabase {
public:
    PostgreSQLDatabase(const std::string& host,
                       const std::string& user,
                       const std::string& password,
                       const std::string& database,
                       unsigned int port = 5432);
    ~PostgreSQLDatabase() override;

    bool execute(const std::string& sql) override;
    std::vector<std::vector<std::string>> query(const std::string& sql) override;
    bool executePrepared(const std::string& sql, const std::vector<std::string>& params) override;
    std::vector<std::vector<std::string>> queryPrepared(const std::string& sql, const std::vector<std::string>& params) override;
    void close() override;

    virtual DatabaseType getType() const override;

private:
    PGconn* conn_;
};

} // namespace db
} // namespace my_mod
*/