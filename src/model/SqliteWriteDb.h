#pragma once

#include "model/Sqlite3Api.h"

#include <stdexcept>
#include <string>

namespace OpenMSViewer
{
  /**
   * @brief Minimal writable sqlite3 handle for building fixtures in tests/tools.
   *
   * OpenMS vendors sqlite3 but no longer exposes its C++ SqliteConnector's
   * sqlite3* helpers in installed headers, so this wraps the raw C ABI (already
   * declared in Sqlite3Api.h and exported by libOpenMS) the same way the
   * viewer's read-side code does. It opens read/write, creating the file if
   * needed, and throws std::runtime_error on any failure. Production code stays
   * read-only.
   */
  class SqliteWriteDb
  {
  public:
    explicit SqliteWriteDb(const std::string& path)
    {
      if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                          nullptr) != SQLITE_OK)
      {
        const std::string message = db_ ? sqlite3_errmsg(db_) : "out of memory";
        if (db_) sqlite3_close(db_);
        throw std::runtime_error("SqliteWriteDb: cannot open '" + path + "': " + message);
      }
    }

    ~SqliteWriteDb() { if (db_) sqlite3_close(db_); }

    SqliteWriteDb(const SqliteWriteDb&) = delete;
    SqliteWriteDb& operator=(const SqliteWriteDb&) = delete;

    /// Execute one or more SQL statements; throws on error.
    void executeStatement(const std::string& statement)
    {
      char* error = nullptr;
      if (sqlite3_exec(db_, statement.c_str(), nullptr, nullptr, &error) != SQLITE_OK)
      {
        const std::string message = error ? error : "unknown error";
        sqlite3_free(error);
        throw std::runtime_error("SqliteWriteDb: statement failed: " + message);
      }
    }

    sqlite3* handle() const { return db_; }

  private:
    sqlite3* db_{nullptr};
  };
}
