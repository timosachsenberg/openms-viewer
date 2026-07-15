#pragma once

// OpenMS statically vendors sqlite3 but does not expose <sqlite3.h> on the
// public include path. Its shared library exposes the C symbols on Unix; on
// Windows the viewer explicitly links OpenMS's sqlite3.lib because DLL exports
// are opt-in. Declaring the small, ABI-stable slice here avoids hard-coding a
// build-tree include path. The sqlite3 C ABI is a public stability guarantee.

extern "C"
{
  typedef struct sqlite3 sqlite3;
  typedef struct sqlite3_stmt sqlite3_stmt;
  typedef long long int sqlite3_int64;

  int sqlite3_open_v2(const char* filename, sqlite3** ppDb, int flags, const char* zVfs);
  int sqlite3_close(sqlite3* db);
  const char* sqlite3_errmsg(sqlite3* db);

  int sqlite3_prepare_v2(sqlite3* db, const char* zSql, int nByte,
                         sqlite3_stmt** ppStmt, const char** pzTail);
  int sqlite3_step(sqlite3_stmt* stmt);
  int sqlite3_finalize(sqlite3_stmt* stmt);

  // Write side, used only to build sqlite fixtures in tests/tools (see
  // SqliteWriteDb.h); production code opens read-only.
  int sqlite3_exec(sqlite3* db, const char* sql,
                   int (*callback)(void*, int, char**, char**), void* arg, char** errmsg);
  void sqlite3_free(void* ptr);

  int sqlite3_bind_text(sqlite3_stmt* stmt, int index, const char* text, int nByte,
                        void (*destructor)(void*));
  int sqlite3_bind_int64(sqlite3_stmt* stmt, int index, sqlite3_int64 value);

  int sqlite3_column_type(sqlite3_stmt* stmt, int column);
  int sqlite3_column_int(sqlite3_stmt* stmt, int column);
  sqlite3_int64 sqlite3_column_int64(sqlite3_stmt* stmt, int column);
  double sqlite3_column_double(sqlite3_stmt* stmt, int column);
  const unsigned char* sqlite3_column_text(sqlite3_stmt* stmt, int column);
}

// Result codes / type codes / open flags used by OswStore.
#define SQLITE_OK 0
#define SQLITE_ROW 100
#define SQLITE_NULL 5
#define SQLITE_OPEN_READONLY 0x00000001
#define SQLITE_OPEN_READWRITE 0x00000002
#define SQLITE_OPEN_CREATE 0x00000004
#define SQLITE_TRANSIENT ((void (*)(void*)) -1)
