/* Minimal loadable SQLite extension for `sql query --load-extension` tests. */
#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1

static void csvzall_test_answer(sqlite3_context* context, int argc, sqlite3_value** argv) {
  (void)argc;
  (void)argv;
  sqlite3_result_int(context, 42);
}

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_extension_init(sqlite3* db, char** error, const sqlite3_api_routines* api) {
  (void)error;
  SQLITE_EXTENSION_INIT2(api);
  return sqlite3_create_function(db, "csvzall_test_answer", 0,
                                 SQLITE_UTF8 | SQLITE_DETERMINISTIC, 0,
                                 csvzall_test_answer, 0, 0);
}
