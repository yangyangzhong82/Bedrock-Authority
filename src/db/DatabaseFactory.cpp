#include "db/DatabaseFactory.h"
#include "db/SQLiteDatabase.h"
#if BA_ENABLE_MYSQL
#include "db/MySQLDatabase.h"
#endif
#if BA_ENABLE_POSTGRESQL
#include "db/PostgreSQLDatabase.h"
#endif
#include <memory> // 确保包含 unique_ptr
#include <stdexcept>
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
#if BA_ENABLE_MYSQL
    return std::make_unique<MySQLDatabase>(host, user, password, database, port);
#else
    (void)host;
    (void)user;
    (void)password;
    (void)database;
    (void)port;
    throw std::runtime_error("MySQL support is disabled in this build");
#endif
}

std::unique_ptr<IDatabase> DatabaseFactory::createPostgreSQL(const std::string& host,
                                                              const std::string& user,
                                                              const std::string& password,
                                                              const std::string& database,
                                                              unsigned int port) {
#if BA_ENABLE_POSTGRESQL
    return std::make_unique<PostgreSQLDatabase>(host, user, password, database, port);
#else
    (void)host;
    (void)user;
    (void)password;
    (void)database;
    (void)port;
    throw std::runtime_error("PostgreSQL support is disabled in this build");
#endif
}

} // namespace db
} // namespace BA
