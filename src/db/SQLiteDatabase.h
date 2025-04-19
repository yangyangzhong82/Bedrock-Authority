#pragma once

#include "db/IDatabase.h"
#include <sqlite3.h>
#include <string>
#include <vector>

namespace BA {
namespace db {

class SQLiteDatabase : public IDatabase {
public:
    explicit SQLiteDatabase(const std::string& dbPath);
    ~SQLiteDatabase() override;

    bool execute(const std::string& sql) override;
    std::vector<std::vector<std::string>> query(const std::string& sql) override;
    bool executePrepared(const std::string& sql, const std::vector<std::string>& params) override;
    std::vector<std::vector<std::string>> queryPrepared(const std::string& sql, const std::vector<std::string>& params) override;
    void close() override;

private:
    sqlite3* db_;
};

} // namespace db
} // namespace my_mod
