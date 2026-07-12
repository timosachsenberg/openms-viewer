#pragma once

// OpenMS statically vendors sqlite3 and re-exports its C symbols from
// libOpenMS, but does not expose <sqlite3.h> on the public include path. Rather
// than hard-code a build-tree header location, declare the small, ABI-stable
// slice of the sqlite3 C API that OswStore uses. The sqlite3 C ABI is a
// long-standing stability guarantee, so these prototypes are safe to hand-declare.

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
#define SQLITE_TRANSIENT ((void (*)(void*)) -1)
