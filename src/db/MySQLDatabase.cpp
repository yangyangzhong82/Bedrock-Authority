#include "db/MySQLDatabase.h"
#include <stdexcept>
#include "ll/api/mod/NativeMod.h"
#include "ll/api/io/Logger.h"

namespace BA {
namespace db {

MySQLDatabase::MySQLDatabase(const std::string& host,
                             const std::string& user,
                             const std::string& password,
                             const std::string& database,
                             unsigned int port)
    : conn_(mysql_init(nullptr)) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.info("Initializing MySQL connection to %s:%u db=%s as user %s", host.c_str(), port, database.c_str(), user.c_str());
    if (conn_ == nullptr) {
        logger.error("Failed to initialize MySQL connection");
        throw std::runtime_error("Failed to initialize MySQL connection");
    }
    if (!mysql_real_connect(conn_, host.c_str(), user.c_str(), password.c_str(), database.c_str(), port, nullptr, 0)) {
        std::string err = mysql_error(conn_);
        logger.error("Failed to connect to MySQL: %s", err.c_str());
        mysql_close(conn_);
        throw std::runtime_error("Failed to connect to MySQL: " + err);
    }
    logger.info("Connected to MySQL successfully");
}

MySQLDatabase::~MySQLDatabase() {
    close();
}

bool MySQLDatabase::execute(const std::string& sql) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.debug("MySQL execute: %s", sql.c_str());
    if (mysql_query(conn_, sql.c_str()) != 0) {
        logger.error("MySQL execute error: %s", mysql_error(conn_));
        return false;
    }
    logger.debug("MySQL execute succeeded");
    return true;
}

std::vector<std::vector<std::string>> MySQLDatabase::query(const std::string& sql) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.debug("MySQL query: %s", sql.c_str());
    std::vector<std::vector<std::string>> result;
    if (mysql_query(conn_, sql.c_str()) != 0) {
        logger.error("MySQL query error: %s", mysql_error(conn_));
        return result;
    }
    MYSQL_RES* res = mysql_store_result(conn_);
    if (res == nullptr) {
        logger.warn("MySQL query returned no result set");
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
    logger.debug("MySQL query returned %zu rows", result.size());
    return result;
}

void MySQLDatabase::close() {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    if (conn_) {
        logger.info("Closing MySQL connection");
        mysql_close(conn_);
        conn_ = nullptr;
        logger.info("MySQL connection closed");
    }
}


bool MySQLDatabase::executePrepared(const std::string& sql, const std::vector<std::string>& params) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.debug("MySQL executePrepared: %s", sql.c_str());

    MYSQL_STMT* stmt = mysql_stmt_init(conn_);
    if (!stmt) {
        logger.error("MySQL mysql_stmt_init failed");
        return false;
    }

    // Explicitly cast sql.length() to unsigned long
    if (mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.length())) != 0) {
        logger.error("MySQL mysql_stmt_prepare failed: %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    // Bind parameters
    std::vector<MYSQL_BIND> bind(params.size());
    std::vector<unsigned long> lengths(params.size()); // Store lengths for bind
    for (size_t i = 0; i < params.size(); ++i) {
        bind[i] = {}; // Zero initialize
        bind[i].buffer_type = MYSQL_TYPE_STRING;
        // Need const_cast because mysql C API is not const-correct
        bind[i].buffer = const_cast<char*>(params[i].c_str());
        // Explicitly cast params[i].length() to unsigned long
        lengths[i] = static_cast<unsigned long>(params[i].length());
        bind[i].length = &lengths[i];
        bind[i].is_null = 0;
    }

    if (mysql_stmt_bind_param(stmt, bind.data()) != 0) {
        logger.error("MySQL mysql_stmt_bind_param failed: %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    // Execute
    if (mysql_stmt_execute(stmt) != 0) {
        logger.error("MySQL mysql_stmt_execute failed: %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    mysql_stmt_close(stmt);
    logger.debug("MySQL executePrepared succeeded");
    return true;
}


std::vector<std::vector<std::string>> MySQLDatabase::queryPrepared(const std::string& sql, const std::vector<std::string>& params) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.debug("MySQL queryPrepared: %s", sql.c_str());
    std::vector<std::vector<std::string>> result;

    MYSQL_STMT* stmt = mysql_stmt_init(conn_);
    if (!stmt) {
        logger.error("MySQL mysql_stmt_init failed");
        return result;
    }

    // Explicitly cast sql.length() to unsigned long
    if (mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.length())) != 0) {
        logger.error("MySQL mysql_stmt_prepare failed: %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return result;
    }

    // Bind parameters
    std::vector<MYSQL_BIND> param_bind(params.size());
    std::vector<unsigned long> param_lengths(params.size());
    for (size_t i = 0; i < params.size(); ++i) {
        param_bind[i] = {};
        param_bind[i].buffer_type = MYSQL_TYPE_STRING;
        param_bind[i].buffer = const_cast<char*>(params[i].c_str());
        // Explicitly cast params[i].length() to unsigned long
        param_lengths[i] = static_cast<unsigned long>(params[i].length());
        param_bind[i].length = &param_lengths[i];
        param_bind[i].is_null = 0;
    }

    if (mysql_stmt_bind_param(stmt, param_bind.data()) != 0) {
        logger.error("MySQL mysql_stmt_bind_param failed: %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return result;
    }

    // Execute
    if (mysql_stmt_execute(stmt) != 0) {
        logger.error("MySQL mysql_stmt_execute failed: %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return result;
    }

    // Get result metadata
    MYSQL_RES* meta_result = mysql_stmt_result_metadata(stmt);
    if (!meta_result) {
        // This can happen for statements like INSERT/UPDATE/DELETE
        // Check if there's an error or just no result set
        if (mysql_stmt_errno(stmt) != 0) {
             logger.error("MySQL mysql_stmt_result_metadata failed: %s", mysql_stmt_error(stmt));
        } else {
             logger.debug("MySQL queryPrepared did not return a result set (e.g., INSERT/UPDATE).");
        }
        mysql_stmt_close(stmt);
        return result; // Return empty result
    }

    int num_fields = mysql_num_fields(meta_result);

    // Bind result buffers
    std::vector<MYSQL_BIND> result_bind(num_fields);
    std::vector<std::vector<char>> result_buffers(num_fields); // Store data buffers
    std::vector<unsigned long> result_lengths(num_fields);     // Store actual lengths
    // Use C arrays of bool to directly match the compiler's expected bool* type
    bool* is_null_arr = new bool[num_fields]; // Allocate on heap
    bool* error_arr = new bool[num_fields];   // Allocate on heap
    // Ensure cleanup even on early exit
    auto cleanup = [&]() {
        delete[] is_null_arr;
        delete[] error_arr;
    };

    for (int i = 0; i < num_fields; ++i) {
        // Allocate a reasonable buffer size, e.g., 256 bytes.
        // For production, might need dynamic resizing or checking field metadata max_length.
        result_buffers[i].resize(256);
        result_bind[i] = {};
        result_bind[i].buffer_type = MYSQL_TYPE_STRING; // Fetch everything as string
        result_bind[i].buffer = result_buffers[i].data();
        // Explicitly cast result_buffers[i].size() to unsigned long
        result_bind[i].buffer_length = static_cast<unsigned long>(result_buffers[i].size());
        result_bind[i].length = &result_lengths[i];
        // Assign bool* directly, as types now match
        result_bind[i].is_null = &is_null_arr[i];
        result_bind[i].error = &error_arr[i];
    }

    if (mysql_stmt_bind_result(stmt, result_bind.data()) != 0) {
        logger.error("MySQL mysql_stmt_bind_result failed: %s", mysql_stmt_error(stmt));
        cleanup(); // Clean up allocated memory
        mysql_free_result(meta_result);
        mysql_stmt_close(stmt);
        return result;
    }

    // Store result to allow fetching
    if (mysql_stmt_store_result(stmt) != 0) {
         logger.error("MySQL mysql_stmt_store_result failed: %s", mysql_stmt_error(stmt));
         cleanup(); // Clean up allocated memory
         mysql_free_result(meta_result);
         mysql_stmt_close(stmt);
         return result;
    }


    // Fetch rows
    while (mysql_stmt_fetch(stmt) == 0) { // 0 means success
        std::vector<std::string> rowVec;
        rowVec.reserve(num_fields);
        for (int i = 0; i < num_fields; ++i) {
             // Read from the bool C arrays
            if (is_null_arr[i]) { // Check the bool value directly
                rowVec.emplace_back(""); // Or handle NULL as needed
            } else {
                // Check if buffer was too small (though unlikely fetching as string with decent buffer)
                if (error_arr[i]) { // Check the bool value directly
                     logger.warn("MySQL fetch truncation/error in column %d", i);
                     // Handle error, maybe fetch again with larger buffer if needed
                     rowVec.emplace_back(""); // Placeholder for now
                } else {
                    rowVec.emplace_back(result_buffers[i].data(), result_lengths[i]);
                }
            }
        }
        result.push_back(std::move(rowVec));
    }

    // Check for fetch errors after the loop
    if (mysql_stmt_errno(stmt) != 0 && mysql_stmt_errno(stmt) != MYSQL_NO_DATA) {
         logger.error("MySQL mysql_stmt_fetch failed: %s", mysql_stmt_error(stmt));
         // Result might be partially filled
    }


    // Clean up allocated C arrays
    cleanup();

    mysql_free_result(meta_result);
    mysql_stmt_close(stmt);
    logger.debug("MySQL queryPrepared returned %zu rows", result.size());
    return result;
}


} // namespace db
} // namespace my_mod
