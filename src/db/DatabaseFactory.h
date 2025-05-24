#pragma once

#include <memory>
#include <string>
#include "db/IDatabase.h"

namespace BA {
namespace db {

class DatabaseFactory {
public:
    /// Create an SQLite database instance with the given file path
    static std::unique_ptr<IDatabase> createSQLite(const std::string& dbPath);

    /// Create a MySQL database instance with connection parameters
    static std::unique_ptr<IDatabase> createMySQL(const std::string& host,
                                                  const std::string& user,
                                                  const std::string& password,
                                                  const std::string& database,
                                                  unsigned int port = 3306);
    static std::unique_ptr<IDatabase> createPostgreSQL(const std::string& host,
                                                    const std::string& user,
                                                    const std::string& password,
                                                    const std::string& database,
                                                    unsigned int port = 5432);


};


} // namespace db
} // namespace my_mod
