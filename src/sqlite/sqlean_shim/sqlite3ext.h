/*
 * Minimal sqlite3ext.h for compiling sqlean modules into csvzall.
 *
 * sqlean modules include "sqlite3ext.h" so they can also be built as loadable
 * extensions. csvzall links them statically against SQLiteCpp's bundled
 * SQLite with SQLITE_CORE defined, where the real header reduces to
 * sqlite3.h plus no-op SQLITE_EXTENSION_INIT macros. SQLiteCpp does not ship
 * sqlite3ext.h, so this shim provides exactly that core-mode subset.
 */
#ifndef CSVZALL_SQLEAN_SQLITE3EXT_H
#define CSVZALL_SQLEAN_SQLITE3EXT_H

#include <sqlite3.h>

#ifndef SQLITE_CORE
#error "csvzall compiles sqlean with SQLITE_CORE; loadable-extension builds need SQLite's real sqlite3ext.h"
#endif

#define SQLITE_EXTENSION_INIT1 /* no-op */
#define SQLITE_EXTENSION_INIT2(v) (void)(v)
#define SQLITE_EXTENSION_INIT3 /* no-op */

#endif /* CSVZALL_SQLEAN_SQLITE3EXT_H */
