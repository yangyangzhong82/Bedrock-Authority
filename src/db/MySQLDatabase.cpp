#include "db/MySQLDatabase.h"
#include <stdexcept>
#include <string>
#include <algorithm> // for std::search
#include <cctype>    // for std::tolower
#include <memory>    // for std::unique_ptr
#include "ll/api/mod/NativeMod.h"
#include "ll/api/io/Logger.h"

// 辅助函数：不区分大小写的字符串搜索
bool containsCaseInsensitive(const std::string& haystack, const std::string& needle) {
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](unsigned char ch1, unsigned char ch2) { return std::tolower(ch1) == std::tolower(ch2); }
    );
    return (it != haystack.end());
}

namespace BA {
namespace db {

MySQLDatabase::MySQLDatabase(const std::string& host,
                             const std::string& user,
                             const std::string& password,
                             const std::string& database,
                              unsigned int port)
    : conn_(mysql_init(nullptr)) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.info("正在初始化 MySQL 连接到 {}:{} 数据库={} 用户={}", host, port, database, user);
    if (conn_ == nullptr) {
        logger.error("初始化 MySQL 连接失败");
        throw std::runtime_error("初始化 MySQL 连接失败");
    }
    if (!mysql_real_connect(conn_, host.c_str(), user.c_str(), password.c_str(), database.c_str(), port, nullptr, 0)) {
        std::string err = mysql_error(conn_);
        logger.error("连接到 MySQL 失败: {}", err);
        mysql_close(conn_);
        throw std::runtime_error("连接到 MySQL 失败: " + err);
    }
    logger.info("成功连接到 MySQL");
}

MySQLDatabase::~MySQLDatabase() {
    close();
}

bool MySQLDatabase::execute(const std::string& sql) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.debug("MySQL 执行: {}", sql); // 对 std::string 使用 {}
    if (mysql_query(conn_, sql.c_str()) != 0) {
        unsigned int error_code = mysql_errno(conn_); // 获取错误代码
        std::string error_msg = mysql_error(conn_); // 获取错误消息
         // 检查 "Duplicate entry ..." 错误 (ER_DUP_ENTRY - 1062)，通常在 ALTER TABLE 使用默认值创建重复项时发生
         if (error_code == 1062 && containsCaseInsensitive(sql, "ALTER TABLE")) {
             logger.warn("MySQL 执行警告 (已忽略 - ALTER TABLE 期间出现重复条目): {}. 语句: {}", error_msg, sql);
             return true; // 视为成功，假设重复是由添加具有默认值的列引起的
         // 检查特定的 "Can't drop ..., check that column/key exists" 错误 (ER_CANT_DROP_FIELD_OR_KEY - 1091)
         } else if (error_code == 1091 && (containsCaseInsensitive(sql, "DROP COLUMN") || containsCaseInsensitive(sql, "DROP FOREIGN KEY") || containsCaseInsensitive(sql, "DROP PRIMARY KEY") || containsCaseInsensitive(sql, "DROP INDEX"))) {
              logger.warn("MySQL 执行警告 (已忽略 - 删除失败，因为项目不存在): {}. 语句: {}", error_msg, sql);
              return true; // 视为成功，因为要删除的项目不存在
         // 检查 ADD COLUMN 期间的 "Duplicate column name" 错误 (ER_DUP_FIELDNAME - 1060)
         } else if (error_code == 1060 && containsCaseInsensitive(sql, "ADD COLUMN")) {
             logger.warn("MySQL 执行警告 (已忽略 - ADD COLUMN 期间出现重复列名): {}. 语句: {}", error_msg, sql);
             return true; // 视为成功，列已存在
         // 新增：检查 CREATE INDEX 期间的 "Duplicate key name" 错误 (ER_DUP_KEYNAME - 1061)
         } else if (error_code == 1061 && containsCaseInsensitive(sql, "CREATE INDEX")) {
             logger.warn("MySQL 执行警告 (已忽略 - CREATE INDEX 期间出现重复索引名): {}. 语句: {}", error_msg, sql);
             return true; // 视为成功，索引已存在
         } else {
            logger.error("MySQL 执行错误 (代码 {}): {}. 语句: {}", error_code, error_msg, sql); // 记录其他错误
            return false;
        }
    }
    logger.debug("MySQL 执行成功");
    return true;
}

std::vector<std::vector<std::string>> MySQLDatabase::query(const std::string& sql) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.debug("MySQL 查询: {}", sql); // 对 std::string 使用 {}
    std::vector<std::vector<std::string>> result;
    if (mysql_query(conn_, sql.c_str()) != 0) {
        std::string error_msg = mysql_error(conn_); // 先获取错误消息
        logger.error("MySQL 查询错误: {}", error_msg); // 使用 {} 占位符记录 std::string
        return result;
    }
    MYSQL_RES* res = mysql_store_result(conn_);
    if (res == nullptr) {
        logger.warn("MySQL 查询未返回结果集");
        return result;
    }
    int num_fields = mysql_num_fields(res);
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        std::vector<std::string> rowVec;
        for (int i = 0; i < num_fields; ++i) {
            rowVec.emplace_back(row[i] ? row[i] : "");
        }
        result.push_back(std::move(rowVec));
    }
    mysql_free_result(res);
    logger.debug("MySQL 查询返回 {} 行", result.size());
    return result;
}

void MySQLDatabase::close() {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    if (conn_) {
        logger.info("正在关闭 MySQL 连接");
        mysql_close(conn_);
        conn_ = nullptr;
        logger.info("MySQL 连接已关闭");
    }
}

DatabaseType MySQLDatabase::getType() const {
    return DatabaseType::MySQL;
}


bool MySQLDatabase::executePrepared(const std::string& sql, const std::vector<std::string>& params) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.debug("MySQL 执行预处理语句: {}", sql);

    MYSQL_STMT* stmt = mysql_stmt_init(conn_);
    if (!stmt) {
        logger.error("MySQL mysql_stmt_init 失败");
        return false;
    }

    // 显式转换 sql.length() 为 unsigned long
    if (mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.length())) != 0) {
        std::string error_msg = mysql_stmt_error(stmt); // 先获取错误消息
        logger.error("MySQL mysql_stmt_prepare 失败: {}", error_msg); // 使用 {} 占位符记录 std::string
        mysql_stmt_close(stmt);
        return false;
    }

    // 绑定参数
    std::vector<MYSQL_BIND> bind(params.size());
    std::vector<unsigned long> lengths(params.size()); // 存储绑定长度
    for (size_t i = 0; i < params.size(); ++i) {
        bind[i] = {}; // 零初始化
        bind[i].buffer_type = MYSQL_TYPE_STRING;
        bind[i].buffer = const_cast<char*>(params[i].c_str());
        // 显式转换 params[i].length() 为 unsigned long
        lengths[i] = static_cast<unsigned long>(params[i].length());
        bind[i].length = &lengths[i];
        bind[i].is_null = 0;
    }

    if (mysql_stmt_bind_param(stmt, bind.data()) != 0) {
        logger.error("MySQL mysql_stmt_bind_param 失败: {}", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    // 执行
    if (mysql_stmt_execute(stmt) != 0) {
        unsigned int error_code = mysql_stmt_errno(stmt); // 获取错误代码
        std::string error_msg = mysql_stmt_error(stmt); // 获取错误消息
        if (error_code == 1062) { // ER_DUP_ENTRY 表示重复键
            logger.warn("MySQL executePrepared 重复条目已忽略: {}", error_msg);
            // 将重复键错误视为 INSERT IGNORE 语义的成功
            mysql_stmt_close(stmt);
            return true; // 即使执行因重复而失败，也返回 true
        } else {
            logger.error("MySQL mysql_stmt_execute 失败 (代码 {}): {}", error_code, error_msg);
            mysql_stmt_close(stmt);
            return false; // 对其他错误返回 false
        }
    }

    mysql_stmt_close(stmt);
    logger.debug("MySQL executePrepared 成功");
    return true;
}


std::vector<std::vector<std::string>> MySQLDatabase::queryPrepared(const std::string& sql, const std::vector<std::string>& params) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.debug("MySQL 查询预处理语句: {}", sql);
    std::vector<std::vector<std::string>> result;

    MYSQL_STMT* stmt = mysql_stmt_init(conn_);
    if (!stmt) {
        logger.error("MySQL mysql_stmt_init 失败");
        return result;
    }

    // 显式转换 sql.length() 为 unsigned long
    if (mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.length())) != 0) {
        logger.error("MySQL mysql_stmt_prepare 失败: {}", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return result;
    }

    // 绑定参数
    std::vector<MYSQL_BIND> param_bind(params.size());
    std::vector<unsigned long> param_lengths(params.size());
    for (size_t i = 0; i < params.size(); ++i) {
        param_bind[i] = {};
        param_bind[i].buffer_type = MYSQL_TYPE_STRING;
        param_bind[i].buffer = const_cast<char*>(params[i].c_str());
        // 显式转换 params[i].length() 为 unsigned long
        param_lengths[i] = static_cast<unsigned long>(params[i].length());
        param_bind[i].length = &param_lengths[i];
        param_bind[i].is_null = 0;
    }

    if (mysql_stmt_bind_param(stmt, param_bind.data()) != 0) {
        logger.error("MySQL mysql_stmt_bind_param 失败: {}", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return result;
    }

    // 执行
    if (mysql_stmt_execute(stmt) != 0) {
        logger.error("MySQL mysql_stmt_execute 失败: {}", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return result;
    }

    // 获取结果元数据
    MYSQL_RES* meta_result = mysql_stmt_result_metadata(stmt);
    if (!meta_result) {
        // 对于 INSERT/UPDATE/DELETE 等语句可能会发生这种情况
        // 检查是否有错误或只是没有结果集
        if (mysql_stmt_errno(stmt) != 0) {
         logger.error("MySQL mysql_stmt_result_metadata 失败: {}", mysql_stmt_error(stmt));
        } else {
             logger.debug("MySQL queryPrepared 未返回结果集 (例如 INSERT/UPDATE)。");
        }
        mysql_stmt_close(stmt);
        return result; // 返回空结果
    }

    int num_fields = mysql_num_fields(meta_result);

    // 绑定结果缓冲区
    std::vector<MYSQL_BIND> result_bind(num_fields);
    std::vector<std::vector<char>> result_buffers(num_fields); // 存储数据缓冲区
    std::vector<unsigned long> result_lengths(num_fields);     // 存储实际长度
    // 使用智能指针管理动态分配的 bool 数组
    std::unique_ptr<bool[]> is_null_arr(new bool[num_fields]);
    std::unique_ptr<bool[]> error_arr(new bool[num_fields]);

    for (int i = 0; i < num_fields; ++i) {
        // 分配一个合理的缓冲区大小，例如 1024 字节。
        // 对于生产环境，可能需要动态调整大小或检查字段元数据的 max_length。
        result_buffers[i].resize(1024);
        result_bind[i] = {};
        result_bind[i].buffer_type = MYSQL_TYPE_STRING; // 将所有内容作为字符串获取
        result_bind[i].buffer = result_buffers[i].data();
        // 显式转换 result_buffers[i].size() 为 unsigned long
        result_bind[i].buffer_length = static_cast<unsigned long>(result_buffers[i].size());
        result_bind[i].length = &result_lengths[i];
        // 直接分配 bool*，因为类型现在匹配
        result_bind[i].is_null = &is_null_arr[i];
        result_bind[i].error = &error_arr[i];
    }

    if (mysql_stmt_bind_result(stmt, result_bind.data()) != 0) {
        logger.error("MySQL mysql_stmt_bind_result 失败: {}", mysql_stmt_error(stmt));
        mysql_free_result(meta_result);
        mysql_stmt_close(stmt);
        return result;
    }

    // 存储结果以允许获取
    if (mysql_stmt_store_result(stmt) != 0) {
         logger.error("MySQL mysql_stmt_store_result 失败: {}", mysql_stmt_error(stmt));
         mysql_free_result(meta_result);
         mysql_stmt_close(stmt);
         return result;
    }


    // 获取行
    while (mysql_stmt_fetch(stmt) == 0) { // 0 表示成功
        std::vector<std::string> rowVec;
        rowVec.reserve(num_fields);
        for (int i = 0; i < num_fields; ++i) {
            // 从 bool C 数组读取
            if (is_null_arr[i]) { // 直接检查 bool 值
                rowVec.emplace_back(""); // 处理 NULL 值
            } else {
                // 检查是否发生截断
                if (error_arr[i]) { // 直接检查 bool 值
                    logger.warn("MySQL 获取第 {} 列时发生截断。实际长度: {}", i, result_lengths[i]);
                    // 重新分配缓冲区并重新获取该列
                    result_buffers[i].resize(result_lengths[i] + 1); // +1 for null terminator
                    result_bind[i].buffer = result_buffers[i].data();
                    result_bind[i].buffer_length = static_cast<unsigned long>(result_buffers[i].size());

                    if (mysql_stmt_fetch_column(stmt, &result_bind[i], i, 0) != 0) {
                        logger.error("MySQL mysql_stmt_fetch_column 失败: {}", mysql_stmt_error(stmt));
                        rowVec.emplace_back(""); // 发生错误时使用空字符串
                    } else {
                        rowVec.emplace_back(result_buffers[i].data(), result_lengths[i]);
                    }
                } else {
                    rowVec.emplace_back(result_buffers[i].data(), result_lengths[i]);
                }
            }
        }
        result.push_back(std::move(rowVec));
    }

    // 循环后检查获取错误
    if (mysql_stmt_errno(stmt) != 0 && mysql_stmt_errno(stmt) != MYSQL_NO_DATA) {
        logger.error("MySQL mysql_stmt_fetch 失败: {}", mysql_stmt_error(stmt));
        // 结果可能部分填充
    }

    mysql_free_result(meta_result);
    mysql_stmt_close(stmt);
    logger.debug("MySQL queryPrepared 返回 {} 行", result.size());
    return result;
}


// 新增：获取创建表的 SQL 语句
std::string MySQLDatabase::getCreateTableSql(const std::string& tableName, const std::string& columns) const {
    return "CREATE TABLE IF NOT EXISTS " + tableName + " (" + columns + ");";
}

// 新增：获取添加列的 SQL 语句
std::string MySQLDatabase::getAddColumnSql(const std::string& tableName, const std::string& columnName, const std::string& columnDefinition) const {
    return "ALTER TABLE " + tableName + " ADD COLUMN " + columnName + " " + columnDefinition + ";";
}

// 新增：获取创建索引的 SQL 语句
std::string MySQLDatabase::getCreateIndexSql(const std::string& indexName, const std::string& tableName, const std::string& columnName) const {
    // MySQL does not support IF NOT EXISTS for CREATE INDEX directly.
    // The MySQLDatabase::execute method is expected to handle the duplicate key error (1061) as a warning.
    return "CREATE INDEX " + indexName + " ON " + tableName + " (" + columnName + ");";
}

// 新增：获取插入或忽略冲突的 SQL 语句 (用于 INSERT IGNORE)
std::string MySQLDatabase::getInsertOrIgnoreSql(const std::string& tableName, const std::string& columns, const std::string& values, const std::string& conflictColumns) const {
    // MySQL uses INSERT IGNORE
    return "INSERT IGNORE INTO " + tableName + " (" + columns + ") VALUES (" + values + ");";
}

// 新增：获取自增主键的数据库方言定义
std::string MySQLDatabase::getAutoIncrementPrimaryKeyDefinition() const {
    return "INT AUTO_INCREMENT PRIMARY KEY";
}

std::string MySQLDatabase::getInClausePlaceholders(size_t count) const {
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

bool MySQLDatabase::beginTransaction() {
    return execute("START TRANSACTION;");
}

bool MySQLDatabase::commit() {
    return execute("COMMIT;");
}

bool MySQLDatabase::rollback() {
    return execute("ROLLBACK;");
}

} // namespace db
} // namespace BA
