#pragma once

#include <string>

namespace BA {
namespace config {

struct Config {
    int version = 2;
    std::string db_type = "sqlite";       // "sqlite" or "mysql"
    std::string sqlite_path = "ba.db";
    std::string mysql_host = "127.0.0.1";
    std::string mysql_user = "root";
    std::string mysql_password;
    std::string mysql_db = "ba";
    unsigned int mysql_port = 3306;
        std::string postgresql_host = "127.0.0.1";
    std::string postgresql_user = "postgres";
    std::string postgresql_password;
    std::string postgresql_db = "ba";
    unsigned int postgresql_port = 5432;
};

} // namespace config
} // namespace my_mod
