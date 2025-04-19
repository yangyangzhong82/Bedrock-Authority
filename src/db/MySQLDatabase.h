#pragma once

#include "db/IDatabase.h"
#include <mysql.h>
#include <string>
#include <vector>

namespace BA {
namespace db {

class MySQLDatabase : public IDatabase {
public:
    MySQLDatabase(const std::string& host,
                  const std::string& user,
                  const std::string& password,
                  const std::string& database,
                  unsigned int port = 3306);
    ~MySQLDatabase() override;

    bool execute(const std::string& sql) override;
    std::vector<std::vector<std::string>> query(const std::string& sql) override;
    bool executePrepared(const std::string& sql, const std::vector<std::string>& params) override;
    std::vector<std::vector<std::string>> queryPrepared(const std::string& sql, const std::vector<std::string>& params) override;
    void close() override;

private:
    MYSQL* conn_;
};

} // namespace db
} // namespace my_mod
