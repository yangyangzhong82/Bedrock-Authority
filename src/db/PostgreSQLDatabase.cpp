
#include "db/PostgreSQLDatabase.h"
#include <stdexcept>
#include "ll/api/mod/NativeMod.h"
#include "ll/api/io/Logger.h"
#include <vector>
#include <string> // 确保包含 string 头文件
#include <algorithm> // 确保包含 algorithm 头文件
#include <functional> // 确保包含 functional 头文件
#include <cctype>

using namespace std; // 引入 std 命名空间
using namespace ll::mod; // 引入 ll::mod 命名空间

// Helper: replace '?' placeholders with '$n' for PostgreSQL
static string replacePlaceholders(const string& sql) {
    string result;
    result.reserve(sql.size());
    int index = 1;
    auto isIdentifierChar = [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '$' || c >= 128;
    };
    for (size_t i = 0; i < sql.size();) {
        size_t start = i;
        if (sql[i] == '\'' || sql[i] == '"') {
            char quote = sql[i++];
            bool escapeString = quote == '\'' && start > 0
                && (sql[start - 1] == 'E' || sql[start - 1] == 'e')
                && (start == 1 || !isIdentifierChar(sql[start - 2]));
            while (i < sql.size()) {
                char c = sql[i++];
                if (escapeString && c == '\\' && i < sql.size()) {
                    ++i;
                } else if (c == quote) {
                    if (i < sql.size() && sql[i] == quote) ++i;
                    else break;
                }
            }
        } else if (sql.compare(i, 2, "--") == 0) {
            i = sql.find_first_of("\r\n", i + 2);
            if (i == string::npos) i = sql.size();
        } else if (sql.compare(i, 2, "/*") == 0) {
            i += 2;
            int depth = 1;
            while (i < sql.size() && depth > 0) {
                if (sql.compare(i, 2, "/*") == 0) {
                    ++depth;
                    i += 2;
                } else if (sql.compare(i, 2, "*/") == 0) {
                    --depth;
                    i += 2;
                } else {
                    ++i;
                }
            }
        } else if (sql[i] == '$' && (i == 0 || !isIdentifierChar(sql[i - 1]))) {
            // PostgreSQL dollar-quoted strings: $$...$$ or $tag$...$tag$.
            size_t end = i + 1;
            if (end < sql.size() && !std::isdigit(static_cast<unsigned char>(sql[end]))) {
                while (end < sql.size() && sql[end] != '$' && isIdentifierChar(sql[end])) ++end;
            }
            if (end < sql.size() && sql[end] == '$') {
                string delimiter = sql.substr(i, end - i + 1);
                size_t closing = sql.find(delimiter, end + 1);
                i = closing == string::npos ? sql.size() : closing + delimiter.size();
            } else {
                ++i;
            }
        } else if (sql[i] == '?') {
            result += '$' + to_string(index++);
            ++i;
            continue;
        } else {
            ++i;
        }
        result.append(sql, start, i - start);
    }
    return result;
}

namespace BA {
namespace db {

PostgreSQLDatabase::PostgreSQLDatabase(const string& host,
                                       const string& user,
                                       const string& password,
                                       const string& database,
                                       unsigned int port)
    : conn_(nullptr) {
    auto& logger = NativeMod::current()->getLogger();
    logger.info("正在初始化 PostgreSQL 连接到 {}:{} 数据库={} 用户={}", host, port, database, user);

    string portString = to_string(port);
    const char* keywords[] = {"host", "port", "dbname", "user", "password", nullptr};
    const char* values[] = {host.c_str(), portString.c_str(), database.c_str(), user.c_str(), password.c_str(), nullptr};
    conn_ = PQconnectdbParams(keywords, values, 0);

    if (PQstatus(conn_) != CONNECTION_OK) {
        string err = PQerrorMessage(conn_);
        logger.error("连接到 PostgreSQL 失败: {}", err);
        PQfinish(conn_);
        throw runtime_error("连接到 PostgreSQL 失败: " + err);
    }
    logger.info("成功连接到 PostgreSQL");
}

PostgreSQLDatabase::~PostgreSQLDatabase() {
    close();
}

bool PostgreSQLDatabase::ensureConnection() {
    if (!conn_) return false;
    if (PQstatus(conn_) == CONNECTION_OK) return true;
    if (transactionActive_) return false;

    PQreset(conn_);
    if (PQstatus(conn_) == CONNECTION_OK) return true;

    NativeMod::current()->getLogger().error("PostgreSQL 重新连接失败: {}", PQerrorMessage(conn_));
    return false;
}

bool PostgreSQLDatabase::execute(const string& sql) {
    std::lock_guard lock(connectionMutex_);
    if (!ensureConnection()) return false;
    auto& logger = NativeMod::current()->getLogger();
    logger.debug("PostgreSQL 执行: {}", sql);

    PGresult* res = PQexec(conn_, sql.c_str());
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        string error_msg = PQerrorMessage(conn_);
        const char* sqlstate = PQresultErrorField(res, PG_DIAG_SQLSTATE);

        // 定义可忽略的 PostgreSQL 错误码
        // 23505: unique_violation (例如，插入重复的主键或唯一约束)
        // 42P07: duplicate_table (例如，CREATE TABLE IF NOT EXISTS 失败，因为表已存在)
        // 42701: duplicate_column (例如，ALTER TABLE ADD COLUMN IF NOT EXISTS 失败，因为列已存在)
        if (sqlstate && (string(sqlstate) == "23505" || string(sqlstate) == "42P07" || string(sqlstate) == "42701")) {
            logger.warn("PostgreSQL 执行警告 (已忽略 - SQLSTATE: {}): {}. 语句: {}", sqlstate, error_msg, sql);
            PQclear(res);
            return true; // 视为成功，因为是可忽略的错误
        } else {
            logger.error("PostgreSQL 执行错误 (SQLSTATE: {}): {}. 语句: {}", sqlstate ? sqlstate : "N/A", error_msg, sql);
            PQclear(res);
            return false;
        }
    }
    PQclear(res);
    logger.debug("PostgreSQL 执行成功");
    return true;
}

vector<vector<string>> PostgreSQLDatabase::query(const string& sql) {
    std::lock_guard lock(connectionMutex_);
    if (!ensureConnection()) return {};
    auto& logger = NativeMod::current()->getLogger();
    logger.debug("PostgreSQL 查询: {}", sql);

    vector<vector<string>> result;
    PGresult* res = PQexec(conn_, sql.c_str());

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        string error_msg = PQerrorMessage(conn_);
        logger.error("PostgreSQL 查询错误: {}", error_msg);
        PQclear(res);
        return result;
    }

    int nFields = PQnfields(res);
    int nRows = PQntuples(res);
    for (int i = 0; i < nRows; i++) {
        vector<string> row;
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
    std::lock_guard lock(connectionMutex_);
    auto& logger = NativeMod::current()->getLogger();
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

bool PostgreSQLDatabase::executePrepared(const string& sql, const SqlParams& params) {
    std::lock_guard lock(connectionMutex_);
    if (!ensureConnection()) return false;
    auto& logger = NativeMod::current()->getLogger();
    string processedSql = replacePlaceholders(sql);
    logger.debug("PostgreSQL 执行预处理语句: {}", processedSql);

    vector<const char*> paramValues;
    paramValues.reserve(params.size());
    for (const auto& param : params) {
        paramValues.push_back(param ? param->c_str() : nullptr);
    }

    PGresult* res = PQexecParams(
        conn_,
        processedSql.c_str(),
        static_cast<int>(params.size()),
        nullptr, // paramTypes[] - infer from usage
        paramValues.data(),
        nullptr, // paramLengths[] - unused for text format
        nullptr, // paramFormats[] - text format
        0        // resultFormat - 0 for text
    );

    if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK) {
        string error_msg = PQerrorMessage(conn_);
        logger.error("PostgreSQL 执行预处理语句失败: {}. 语句: {}", error_msg, sql);
        PQclear(res);
        return false;
    }

    lastWriteChanged_ = PQcmdTuples(res)[0] != '\0' && PQcmdTuples(res)[0] != '0';
    PQclear(res);
    logger.debug("PostgreSQL 执行预处理语句成功");
    return true;
}

vector<vector<string>> PostgreSQLDatabase::queryPrepared(const string& sql, const SqlParams& params) {
    std::lock_guard lock(connectionMutex_);
    lastQuerySucceeded_ = false;
    if (!ensureConnection()) return {};
    auto& logger = NativeMod::current()->getLogger();
    string processedSql = replacePlaceholders(sql);
    logger.debug("PostgreSQL 查询预处理语句: {}", processedSql);

    vector<vector<string>> result;
    vector<const char*> paramValues;
    paramValues.reserve(params.size());
    for (const auto& param : params) {
        paramValues.push_back(param ? param->c_str() : nullptr);
    }

    PGresult* res = PQexecParams(
        conn_,
        processedSql.c_str(),
        static_cast<int>(params.size()),
        nullptr, // paramTypes[] - infer from usage
        paramValues.data(),
        nullptr, // paramLengths[] - unused for text format
        nullptr, // paramFormats[] - text format
        0        // resultFormat - 0 for text
    );

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        string error_msg = PQerrorMessage(conn_);
        logger.error("PostgreSQL 查询预处理语句失败: {}. 语句: {}", error_msg, sql);
        PQclear(res);
        return result;
    }

    int nFields = PQnfields(res);
    int nRows = PQntuples(res);
    for (int i = 0; i < nRows; i++) {
        vector<string> row;
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
    lastQuerySucceeded_ = true;
    logger.debug("PostgreSQL 查询预处理语句返回 {} 行", result.size());
    return result;
}

// 新增：获取创建表的 SQL 语句
string PostgreSQLDatabase::getCreateTableSql(const string& tableName, const string& columns) const {
    return "CREATE TABLE IF NOT EXISTS " + tableName + " (" + columns + ");";
}

// 新增：获取添加列的 SQL 语句
string PostgreSQLDatabase::getAddColumnSql(const string& tableName, const string& columnName, const string& columnDefinition) const {
    return "ALTER TABLE " + tableName + " ADD COLUMN IF NOT EXISTS " + columnName + " " + columnDefinition + ";";
}

// 新增：获取创建索引的 SQL 语句
string PostgreSQLDatabase::getCreateIndexSql(const string& indexName, const string& tableName, const string& columnName) const {
    return "CREATE INDEX IF NOT EXISTS " + indexName + " ON " + tableName + " (" + columnName + ");";
}

// 新增：获取插入或忽略冲突的 SQL 语句 (用于 ON CONFLICT)
string PostgreSQLDatabase::getInsertOrIgnoreSql(const string& tableName, const string& columns, const string& values, const string& conflictColumns) const {
    // PostgreSQL uses ON CONFLICT (column) DO NOTHING
    return "INSERT INTO " + tableName + " (" + columns + ") VALUES (" + values + ") ON CONFLICT (" + conflictColumns + ") DO NOTHING;";
}

// 新增：获取自增主键的数据库方言定义
string PostgreSQLDatabase::getAutoIncrementPrimaryKeyDefinition() const {
    return "SERIAL PRIMARY KEY";
}

std::string PostgreSQLDatabase::getInClausePlaceholders(size_t count) const {
    if (count == 0) {
        return "";
    }
    std::string placeholders;
    for (size_t i = 0; i < count; ++i) {
        placeholders += "?";
        if (i < count - 1) {
            placeholders += ", ";
        }
    }
    return placeholders;
}

bool PostgreSQLDatabase::beginTransaction() {
    return execute("BEGIN;");
}

bool PostgreSQLDatabase::commit() {
    if (PQtransactionStatus(conn_) == PQTRANS_INERROR) return false;
    return execute("COMMIT;");
}

bool PostgreSQLDatabase::rollback() {
    return execute("ROLLBACK;");
}

// 批量接口实现
std::unordered_map<std::string, std::vector<std::string>>
PostgreSQLDatabase::fetchDirectPermissionsOfGroups(const std::vector<std::string>& groupIds) {
    std::unordered_map<std::string, std::vector<std::string>> result;
    if (groupIds.empty()) {
        return result;
    }

    // 使用统一的 ? 占位符，由 queryPrepared 转换为 PostgreSQL 编号。
    std::string sql = "SELECT group_id, permission_rule FROM group_permissions WHERE group_id IN ("
                    + getInClausePlaceholders(groupIds.size()) + ");";

    auto rows = queryPrepared(sql, SqlParams(groupIds.begin(), groupIds.end()));
    for (const auto& row : rows) {
        result[row[0]].push_back(row[1]);
    }

    return result;
}

} // namespace db
} // namespace BA
