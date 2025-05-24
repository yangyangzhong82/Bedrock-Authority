#include "db/PostgreSQLDatabase.h"
#include <stdexcept>
#include "ll/api/mod/NativeMod.h"
#include "ll/api/io/Logger.h"
#include <vector>

// Helper: replace '?' placeholders with '$n' for PostgreSQL
static std::string replacePlaceholders(const std::string& sql) {
    std::string result;
    result.reserve(sql.size());
    int index = 1;
    for (char c : sql) {
        if (c == '?') {
            result += '$' + std::to_string(index++);
        } else {
            result += c;
        }
    }
    return result;
}

namespace BA {
namespace db {

PostgreSQLDatabase::PostgreSQLDatabase(const std::string& host,
                                       const std::string& user,
                                       const std::string& password,
                                       const std::string& database,
                                       unsigned int port)
    : conn_(nullptr) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.info("正在初始化 PostgreSQL 连接到 %s:%u 数据库=%s 用户=%s", host.c_str(), port, database.c_str(), user.c_str());

    std::string conninfo = "host=" + host + " port=" + std::to_string(port) + " dbname=" + database + " user=" + user + " password=" + password;
    conn_ = PQconnectdb(conninfo.c_str());

    if (PQstatus(conn_) != CONNECTION_OK) {
        std::string err = PQerrorMessage(conn_);
        logger.error("连接到 PostgreSQL 失败: %s", err.c_str());
        PQfinish(conn_);
        throw std::runtime_error("连接到 PostgreSQL 失败: " + err);
    }
    logger.info("成功连接到 PostgreSQL");
}

PostgreSQLDatabase::~PostgreSQLDatabase() {
    close();
}

bool PostgreSQLDatabase::execute(const std::string& sql) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.debug("PostgreSQL 执行: {}", sql);

    PGresult* res = PQexec(conn_, sql.c_str());
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::string error_msg = PQerrorMessage(conn_);
        logger.error("PostgreSQL 执行错误: {}", error_msg);
        PQclear(res);
        return false;
    }
    PQclear(res);
    logger.debug("PostgreSQL 执行成功");
    return true;
}

std::vector<std::vector<std::string>> PostgreSQLDatabase::query(const std::string& sql) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.debug("PostgreSQL 查询: {}", sql);

    std::vector<std::vector<std::string>> result;
    PGresult* res = PQexec(conn_, sql.c_str());

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::string error_msg = PQerrorMessage(conn_);
        logger.error("PostgreSQL 查询错误: {}", error_msg);
        PQclear(res);
        return result;
    }

    int nFields = PQnfields(res);
    int nRows = PQntuples(res);
    for (int i = 0; i < nRows; i++) {
        std::vector<std::string> row;
        for (int j = 0; j < nFields; j++) {
            row.push_back(PQgetvalue(res, i, j));
        }
        result.push_back(std::move(row));
    }

    PQclear(res);
    logger.debug("PostgreSQL 查询返回 {} 行", result.size());
    return result;
}

void PostgreSQLDatabase::close() {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    if (conn_) {
        logger.info("正在关闭 PostgreSQL 连接");
        PQfinish(conn_);
        conn_ = nullptr;
        logger.info("PostgreSQL 连接已关闭");
    }
}

DatabaseType PostgreSQLDatabase::getType() const {
    return DatabaseType::PostgreSQL;
}

bool PostgreSQLDatabase::executePrepared(const std::string& sql, const std::vector<std::string>& params) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
std::string processedSql = replacePlaceholders(sql);
logger.debug("PostgreSQL 执行预处理语句: {}", processedSql);

    std::vector<const char*> paramValues;
    std::vector<int> paramLengths;
    std::vector<int> paramFormats;

    for (const auto& param : params) {
        paramValues.push_back(param.c_str());
        paramLengths.push_back(static_cast<int>(param.length()));
        paramFormats.push_back(0); // 0 for text format
    }

PGresult* res = PQexecParams(
        conn_,
        processedSql.c_str(),
        static_cast<int>(params.size()),
        nullptr, // paramTypes[] - infer from usage
        paramValues.data(),
        paramLengths.data(),
        paramFormats.data(),
        0        // resultFormat - 0 for text
    );

    if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::string error_msg = PQerrorMessage(conn_);
        logger.error("PostgreSQL 执行预处理语句失败: {}. 语句: {}", error_msg, sql);
        PQclear(res);
        return false;
    }

    PQclear(res);
    logger.debug("PostgreSQL 执行预处理语句成功");
    return true;
}

std::vector<std::vector<std::string>> PostgreSQLDatabase::queryPrepared(const std::string& sql, const std::vector<std::string>& params) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
std::string processedSql = replacePlaceholders(sql);
logger.debug("PostgreSQL 查询预处理语句: {}", processedSql);

    std::vector<std::vector<std::string>> result;
    std::vector<const char*> paramValues;
    std::vector<int> paramLengths;
    std::vector<int> paramFormats;

    for (const auto& param : params) {
        paramValues.push_back(param.c_str());
        paramLengths.push_back(static_cast<int>(param.length()));
        paramFormats.push_back(0); // 0 for text format
    }

PGresult* res = PQexecParams(
        conn_,
        processedSql.c_str(),
        static_cast<int>(params.size()),
        nullptr, // paramTypes[] - infer from usage
        paramValues.data(),
        paramLengths.data(),
        paramFormats.data(),
        0        // resultFormat - 0 for text
    );

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::string error_msg = PQerrorMessage(conn_);
        logger.error("PostgreSQL 查询预处理语句失败: {}. 语句: {}", error_msg, sql);
        PQclear(res);
        return result;
    }

    int nFields = PQnfields(res);
    int nRows = PQntuples(res);
    for (int i = 0; i < nRows; i++) {
        std::vector<std::string> row;
        row.reserve(nFields);
        for (int j = 0; j < nFields; j++) {
            char* val = PQgetvalue(res, i, j);
            if (val) {
                row.push_back(val);
            } else {
                row.push_back(""); // Handle NULL values
            }
        }
        result.push_back(std::move(row));
    }

    PQclear(res);
    logger.debug("PostgreSQL 查询预处理语句返回 {} 行", result.size());
    return result;
}

} // namespace db
} // namespace BA
