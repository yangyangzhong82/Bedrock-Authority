#include "db/SQLiteDatabase.h"
#include <stdexcept>
#include "ll/api/mod/NativeMod.h"
#include "ll/api/io/Logger.h"

namespace BA {
namespace db {

SQLiteDatabase::SQLiteDatabase(const std::string& dbPath) : db_(nullptr) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.info("Opening SQLite database: %s", dbPath.c_str());
    if (sqlite3_open(dbPath.c_str(), &db_) != SQLITE_OK) {
        std::string err(sqlite3_errmsg(db_));
        logger.error("Failed to open SQLite database: %s", err.c_str());
        throw std::runtime_error("Failed to open SQLite database: " + err);
    }
    logger.info("SQLite database opened successfully");
}

SQLiteDatabase::~SQLiteDatabase() {
    close();
}

bool SQLiteDatabase::execute(const std::string& sql) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.debug("Executing SQL: %s", sql.c_str());
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string err(errMsg);
        sqlite3_free(errMsg);
        logger.error("SQLite execute error: %s", err.c_str());
        return false;
    }
    logger.debug("SQL executed successfully");
    return true;
}

std::vector<std::vector<std::string>> SQLiteDatabase::query(const std::string& sql) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.debug("Querying SQL: %s", sql.c_str());
    sqlite3_stmt* stmt = nullptr;
    std::vector<std::vector<std::string>> result;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        logger.error("SQLite prepare error: %s", sqlite3_errmsg(db_));
        return result;
    }
    int cols = sqlite3_column_count(stmt);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::vector<std::string> row;
        for (int i = 0; i < cols; ++i) {
            const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            row.push_back(text ? text : "");
        }
        result.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
    logger.debug("Query returned %zu rows", result.size());
    return result;
}

void SQLiteDatabase::close() {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    if (db_) {
        logger.info("Closing SQLite database");
        sqlite3_close(db_);
        db_ = nullptr;
        logger.info("SQLite database closed");
    }
}

bool SQLiteDatabase::executePrepared(const std::string& sql, const std::vector<std::string>& params) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.debug("Executing prepared SQL: %s", sql.c_str());
    sqlite3_stmt* stmt = nullptr;

    // Prepare statement
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        logger.error("SQLite prepare error: %s", sqlite3_errmsg(db_));
        return false;
    }

    // Bind parameters
    for (int i = 0; i < params.size(); ++i) {
        // SQLite bind indices are 1-based
        if (sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_STATIC) != SQLITE_OK) {
            logger.error("SQLite bind error: %s", sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return false;
        }
    }

    // Execute statement
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        logger.error("SQLite execute prepared error: %s", sqlite3_errmsg(db_));
        sqlite3_finalize(stmt);
        return false;
    }

    // Finalize statement
    sqlite3_finalize(stmt);
    logger.debug("Prepared SQL executed successfully");
    return true;
}

std::vector<std::vector<std::string>> SQLiteDatabase::queryPrepared(const std::string& sql, const std::vector<std::string>& params) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.debug("Querying prepared SQL: %s", sql.c_str());
    sqlite3_stmt* stmt = nullptr;
    std::vector<std::vector<std::string>> result;

    // Prepare statement
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        logger.error("SQLite prepare error: %s", sqlite3_errmsg(db_));
        return result; // Return empty result on prepare error
    }

    // Bind parameters
    for (int i = 0; i < params.size(); ++i) {
        // SQLite bind indices are 1-based
        if (sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_STATIC) != SQLITE_OK) {
            logger.error("SQLite bind error: %s", sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return result; // Return empty result on bind error
        }
    }

    // Execute and fetch rows
    int cols = sqlite3_column_count(stmt);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::vector<std::string> row;
        row.reserve(cols); // Reserve space for efficiency
        for (int i = 0; i < cols; ++i) {
            const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            row.push_back(text ? text : "");
        }
        result.push_back(std::move(row));
    }

    // Check for errors during step
    int rc = sqlite3_errcode(db_);
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
         logger.error("SQLite query prepared step error: %s", sqlite3_errmsg(db_));
         // Result might be partially filled, decide if we should clear it or return partial
         // For now, return whatever was collected before the error
    }


    // Finalize statement
    sqlite3_finalize(stmt);
    logger.debug("Prepared query returned %zu rows", result.size());
    return result;
}


} // namespace db
} // namespace my_mod
