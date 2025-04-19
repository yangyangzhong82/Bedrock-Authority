#pragma once

#include <string>

namespace BA {
namespace config {

struct Config {
    int version = 1;
    std::string db_type = "sqlite";       // "sqlite" or "mysql"
    std::string sqlite_path = "my_mod.db";
    std::string mysql_host = "127.0.0.1";
    std::string mysql_user = "root";
    std::string mysql_password;
    std::string mysql_db = "my_mod";
    unsigned int mysql_port = 3306;
};

} // namespace config
} // namespace my_mod
