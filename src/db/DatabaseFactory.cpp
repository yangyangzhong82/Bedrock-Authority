#include "db/DatabaseFactory.h"
#include "db/SQLiteDatabase.h"
#include "db/MySQLDatabase.h"
#include "db/PostgreSQLDatabase.h"
#include <memory> // 确保包含 unique_ptr
#include <string> // 确保包含 string

namespace BA {
namespace db {

std::unique_ptr<IDatabase> DatabaseFactory::createSQLite(const std::string& dbPath) {
    return std::make_unique<SQLiteDatabase>(dbPath);
}

std::unique_ptr<IDatabase> DatabaseFactory::createMySQL(const std::string& host,
                                                         const std::string& user,
                                                         const std::string& password,
                                                         const std::string& database,
                                                         unsigned int port) {
    return std::make_unique<MySQLDatabase>(host, user, password, database, port);
}

std::unique_ptr<IDatabase> DatabaseFactory::createPostgreSQL(const std::string& host,
                                                              const std::string& user,
                                                              const std::string& password,
                                                              const std::string& database,
                                                              unsigned int port) {
    return std::make_unique<PostgreSQLDatabase>(host, user, password, database, port);
}

} // namespace db
} // namespace BA
