#pragma once

#include "db/IDatabase.h"
#include <memory>
#include <string>


namespace BA {
namespace db {

class DatabaseFactory {
public:
    static std::unique_ptr<IDatabase> createSQLite(const std::string& dbPath);

    static std::unique_ptr<IDatabase> createMySQL(
        const std::string& host,
        const std::string& user,
        const std::string& password,
        const std::string& database,
        unsigned int       port = 3306
    );

    static std::unique_ptr<IDatabase> createPostgreSQL(
        const std::string& host,
        const std::string& user,
        const std::string& password,
        const std::string& database,
        unsigned int       port = 5432
    );
};


} // namespace db
} // namespace BA
