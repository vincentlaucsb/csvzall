if(NOT DEFINED SQLiteCpp_SOURCE_DIR)
  message(FATAL_ERROR "SQLiteCpp_SOURCE_DIR is required")
endif()

set(statement_header "${SQLiteCpp_SOURCE_DIR}/include/SQLiteCpp/Statement.h")
set(statement_source "${SQLiteCpp_SOURCE_DIR}/src/Statement.cpp")

file(READ "${statement_header}" header_text)
file(READ "${statement_source}" source_text)

set(header_signature "void bind(const int aIndex, const char*         apValue, const int aSize);")
if(header_text MATCHES "bind\\(const int aIndex, const char\\* +apValue, const int aSize\\)")
  message(STATUS "SQLiteCpp text-with-length bind declaration already patched")
else()
  set(header_anchor "    void bind(const int aIndex, const char*         apValue);\n")
  set(header_insert
"    void bind(const int aIndex, const char*         apValue);
    /**
     * @brief Bind a text value to a parameter \"?\", \"?NNN\", \":VVV\", \"@VVV\" or \"$VVV\" in the SQL prepared statement (aIndex >= 1)
     *
     * Binds the text using an explicit byte length, so the data does not need to be null-terminated.
     *
     * @note Uses the SQLITE_TRANSIENT flag, making a copy of the data, for SQLite internal use
     */
    void bind(const int aIndex, const char*         apValue, const int aSize);
")
  string(FIND "${header_text}" "${header_anchor}" header_anchor_pos)
  if(header_anchor_pos EQUAL -1)
    message(FATAL_ERROR "Unable to patch SQLiteCpp Statement.h: anchor not found")
  endif()
  string(REPLACE "${header_anchor}" "${header_insert}" header_text "${header_text}")
  file(WRITE "${statement_header}" "${header_text}")
  message(STATUS "Patched SQLiteCpp Statement.h")
endif()

if(source_text MATCHES "sqlite3_bind_text\\(getPreparedStatement\\(\\), aIndex, apValue, aSize, SQLITE_TRANSIENT\\)")
  message(STATUS "SQLiteCpp text-with-length bind implementation already patched")
else()
  set(source_anchor
"void Statement::bind(const int aIndex, const char* apValue)
{
    const int ret = sqlite3_bind_text(getPreparedStatement(), aIndex, apValue, -1, SQLITE_TRANSIENT);
    check(ret);
}
")
  set(source_insert
"void Statement::bind(const int aIndex, const char* apValue)
{
    const int ret = sqlite3_bind_text(getPreparedStatement(), aIndex, apValue, -1, SQLITE_TRANSIENT);
    check(ret);
}

// Bind a text value with explicit length (does not require null termination)
void Statement::bind(const int aIndex, const char* apValue, const int aSize)
{
    const int ret = sqlite3_bind_text(getPreparedStatement(), aIndex, apValue, aSize, SQLITE_TRANSIENT);
    check(ret);
}
")
  string(FIND "${source_text}" "${source_anchor}" source_anchor_pos)
  if(source_anchor_pos EQUAL -1)
    message(FATAL_ERROR "Unable to patch SQLiteCpp Statement.cpp: anchor not found")
  endif()
  string(REPLACE "${source_anchor}" "${source_insert}" source_text "${source_text}")
  file(WRITE "${statement_source}" "${source_text}")
  message(STATUS "Patched SQLiteCpp Statement.cpp")
endif()
