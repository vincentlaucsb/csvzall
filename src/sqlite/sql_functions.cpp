#include "sql_functions.hpp"

#include "../pipeline/common/flexible_date.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <sqlite3.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef CSVZALL_SQLEAN_VERSION
#error "CSVZALL_SQLEAN_VERSION must come from the csvzall_sqlean CMake target"
#endif

// Module entry points from the sqlean sources compiled into csvzall_sqlean.
extern "C" {
int crypto_init(sqlite3* db);
int fuzzy_init(sqlite3* db);
int regexp_init(sqlite3* db);
int stats_init(sqlite3* db);
int text_init(sqlite3* db);
int uuid_init(sqlite3* db);
}

namespace csvzall::sqlite {
namespace {

namespace common = ::csvzall::pipeline::common;

constexpr int kPureFunctionFlags = SQLITE_UTF8 | SQLITE_DETERMINISTIC | SQLITE_INNOCUOUS;

[[noreturn]] void ThrowSqliteError(sqlite3* db, const std::string& context) {
  throw std::runtime_error(context + ": " + sqlite3_errmsg(db));
}

void CheckInit(sqlite3* db, int rc, const char* module) {
  if (rc != SQLITE_OK) {
    ThrowSqliteError(db, std::string("failed to register sqlean ") + module + " functions");
  }
}

void SqleanVersionFunction(sqlite3_context* context, int, sqlite3_value**) {
  sqlite3_result_text(context, CSVZALL_SQLEAN_VERSION, -1, SQLITE_STATIC);
}

struct DateFunction {
  const char* name;
  bool precision_only;
};

constexpr DateFunction kParseDate{"parse_date", false};
constexpr DateFunction kDatePrecision{"date_precision", true};

std::optional<common::DateOrder> DateOrderArgument(sqlite3_context* context,
                                                   int argc,
                                                   sqlite3_value** argv,
                                                   const DateFunction& function) {
  if (argc < 2) {
    return common::DateOrder::MonthDay;
  }
  const auto* text = sqlite3_value_text(argv[1]);
  const std::string order = text ? reinterpret_cast<const char*>(text) : "";
  if (sqlite3_strnicmp(order.c_str(), "mdy", 4) == 0) {
    return common::DateOrder::MonthDay;
  }
  if (sqlite3_strnicmp(order.c_str(), "dmy", 4) == 0) {
    return common::DateOrder::DayMonth;
  }
  const std::string message =
      std::string(function.name) + ": order must be 'mdy' or 'dmy'";
  sqlite3_result_error(context, message.c_str(), -1);
  return std::nullopt;
}

void DateFunctionImpl(sqlite3_context* context, int argc, sqlite3_value** argv) {
  const auto* function = static_cast<const DateFunction*>(sqlite3_user_data(context));
  const auto order = DateOrderArgument(context, argc, argv, *function);
  if (!order) {
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }

  const auto* text = reinterpret_cast<const char*>(sqlite3_value_text(argv[0]));
  const int bytes = sqlite3_value_bytes(argv[0]);
  if (!text) {
    sqlite3_result_null(context);
    return;
  }
  const auto date =
      common::ParsePartialDate(std::string_view(text, static_cast<std::size_t>(bytes)), *order);
  if (!date) {
    sqlite3_result_null(context);
    return;
  }

  if (function->precision_only) {
    const auto name = common::DatePrecisionName(date->precision);
    sqlite3_result_text(context, name.data(), static_cast<int>(name.size()), SQLITE_STATIC);
    return;
  }
  const auto iso = common::FormatIsoDate(*date);
  sqlite3_result_text(context, iso.c_str(), static_cast<int>(iso.size()), SQLITE_TRANSIENT);
}

void RegisterDateFunction(sqlite3* db, const DateFunction& function) {
  for (const int arg_count : {1, 2}) {
    if (sqlite3_create_function(db, function.name, arg_count, kPureFunctionFlags,
                                const_cast<DateFunction*>(&function), DateFunctionImpl,
                                nullptr, nullptr) != SQLITE_OK) {
      ThrowSqliteError(db, std::string("failed to register ") + function.name);
    }
  }
}

}  // namespace

std::string_view SqleanVersion() {
  return CSVZALL_SQLEAN_VERSION;
}

void RegisterCsvzallSqliteFunctions(SQLite::Database& database) {
  sqlite3* db = database.getHandle();
  CheckInit(db, crypto_init(db), "crypto");
  CheckInit(db, fuzzy_init(db), "fuzzy");
  CheckInit(db, regexp_init(db), "regexp");
  CheckInit(db, stats_init(db), "stats");
  CheckInit(db, text_init(db), "text");
  CheckInit(db, uuid_init(db), "uuid");

  if (sqlite3_create_function(db, "sqlean_version", 0, kPureFunctionFlags, nullptr,
                              SqleanVersionFunction, nullptr, nullptr) != SQLITE_OK) {
    ThrowSqliteError(db, "failed to register sqlean_version");
  }
  RegisterDateFunction(db, kParseDate);
  RegisterDateFunction(db, kDatePrecision);
}

void LoadSqliteExtensions(SQLite::Database& database, const std::vector<std::string>& paths) {
  if (paths.empty()) {
    return;
  }
#ifdef SQLITE_OMIT_LOAD_EXTENSION
  throw std::runtime_error("this csvzall build does not support loading SQLite extensions");
#else
  sqlite3* db = database.getHandle();
  if (sqlite3_db_config(db, SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 1, nullptr) != SQLITE_OK) {
    ThrowSqliteError(db, "failed to enable SQLite extension loading");
  }
  // Close the C-API door again once the requested extensions are in, even on
  // failure. SQL-level load_extension() is never enabled.
  struct DisableExtensionLoading {
    sqlite3* db;
    ~DisableExtensionLoading() {
      sqlite3_db_config(db, SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 0, nullptr);
    }
  } disable{db};

  for (const auto& path : paths) {
    char* error = nullptr;
    if (sqlite3_load_extension(db, path.c_str(), nullptr, &error) != SQLITE_OK) {
      std::string message = "cannot load SQLite extension " + path + ": " +
                            (error ? error : sqlite3_errmsg(db));
      sqlite3_free(error);
      throw std::runtime_error(message);
    }
  }
#endif
}

}  // namespace csvzall::sqlite
