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

    // HTTP Server Config
    bool http_server_enabled = true;
    std::string http_server_host = "0.0.0.0";
    unsigned int http_server_port = 8080;
    std::string http_server_static_path = "http_static";

    // Cache Warmup Config
    bool enable_cache_warmup = true; // 是否在启动时预热缓存
    unsigned int cache_worker_threads = 4; // 缓存失效工作线程池大小

    // Cleanup Scheduler Config
    long long cleanup_interval_seconds = 3600; // 定期清理任务的执行间隔（秒），默认为 1 小时
};

} // namespace config
} // namespace BA
