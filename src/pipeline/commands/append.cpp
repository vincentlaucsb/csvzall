#include "commands.hpp"

#include "../common/column_lookup.hpp"
#include "../common/file_replace.hpp"

#include <csv.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace csvzall::pipeline::commands {
namespace {

using common::MoveIntoPlaceNoReplace;
using common::ReplaceFileWith;
using common::TempSiblingFor;

csv::CSVFormat MakeFormat() {
  csv::CSVFormat format;
  format.delimiter({',', '|', '\t', ';', '^'}).quote('"').header_row(0);
  return format;
}

csv::CSVReader OpenReader(const std::string& path) {
  return csv::CSVReader(path, MakeFormat());
}

std::vector<std::string> HeadersFor(const std::string& path) {
  auto reader = OpenReader(path);
  return reader.get_col_names();
}

void ValidateHeadersMatch(const std::vector<std::string>& existing_headers,
                          const std::vector<std::string>& incoming_headers) {
  if (existing_headers != incoming_headers) {
    throw std::runtime_error("existing and incoming CSV headers do not match exactly");
  }
}

std::size_t RequiredKeyIndex(const std::vector<std::string>& headers,
                             const std::string& key_column,
                             std::string_view label) {
  const auto index = common::FindColumnIndex(headers, key_column, true);
  if (!index) {
    throw std::runtime_error("merge: key column '" + key_column +
                             "' not found in " + std::string(label) + " CSV");
  }
  return *index;
}

std::unordered_set<std::string> ReadExistingKeys(const std::string& existing_path,
                                                 const std::vector<std::string>& headers,
                                                 const std::string& key_column) {
  const auto key_index = RequiredKeyIndex(headers, key_column, "existing");
  std::unordered_set<std::string> keys;

  auto existing_reader = OpenReader(existing_path);
  for (auto& row : existing_reader) {
    const auto key = row[key_index].get<std::string>();
    if (!keys.insert(key).second) {
      throw std::runtime_error("merge: duplicate key already present in existing CSV: " + key);
    }
  }
  return keys;
}

std::size_t ValidateIncomingKeys(const std::string& incoming_path,
                                 const std::vector<std::string>& headers,
                                 const std::string& key_column,
                                 const std::unordered_set<std::string>& existing_keys) {
  const auto key_index = RequiredKeyIndex(headers, key_column, "incoming");
  std::size_t skipped_count = 0;

  std::unordered_set<std::string> incoming_keys;
  auto incoming_reader = OpenReader(incoming_path);
  for (auto& row : incoming_reader) {
    const auto key = row[key_index].get<std::string>();
    if (!incoming_keys.insert(key).second) {
      throw std::runtime_error("merge: duplicate key within incoming CSV: " + key);
    }
    if (existing_keys.find(key) != existing_keys.end()) {
      ++skipped_count;
    }
  }
  return skipped_count;
}

void WriteConcatenated(const std::string& existing_path,
                       const std::string& incoming_path,
                       const std::vector<std::string>& headers,
                       std::ostream& output,
                       RunStats& stats) {
  auto writer = csv::make_csv_writer(output).set_auto_flush(false);
  writer << headers;

  auto existing_reader = OpenReader(existing_path);
  for (auto& row : existing_reader) {
    writer << std::vector<std::string>(row);
    ++stats.rows_processed;
  }

  auto incoming_reader = OpenReader(incoming_path);
  for (auto& row : incoming_reader) {
    writer << std::vector<std::string>(row);
    ++stats.rows_processed;
  }

  writer.flush();
}

// Writes existing rows (when existing_present) followed by incoming rows whose
// key is not in existing_keys. existing_present is false only for merge
// --create when the existing file does not exist yet.
std::size_t WriteMerged(const std::string& existing_path,
                        bool existing_present,
                        const std::string& incoming_path,
                        const std::vector<std::string>& headers,
                        const std::string& key_column,
                        const std::unordered_set<std::string>& existing_keys,
                        std::ostream& output,
                        RunStats& stats) {
  auto writer = csv::make_csv_writer(output).set_auto_flush(false);
  writer << headers;

  if (existing_present) {
    auto existing_reader = OpenReader(existing_path);
    for (auto& row : existing_reader) {
      writer << std::vector<std::string>(row);
      ++stats.rows_processed;
    }
  }

  const auto key_index = RequiredKeyIndex(headers, key_column, "incoming");
  std::size_t added_count = 0;
  auto incoming_reader = OpenReader(incoming_path);
  for (auto& row : incoming_reader) {
    if (existing_keys.find(row[key_index].get<std::string>()) != existing_keys.end()) {
      continue;
    }
    writer << std::vector<std::string>(row);
    ++stats.rows_processed;
    ++added_count;
  }

  writer.flush();
  return added_count;
}

// True only when nothing exists at path (not even a dangling symlink).
// Directories, unreadable files, and other status errors are left for the
// normal open path to report.
bool PathDoesNotExist(const std::string& path) {
  std::error_code ec;
  const auto status = std::filesystem::symlink_status(path, ec);
  return status.type() == std::filesystem::file_type::not_found;
}

void RejectDirectory(const std::string& path, std::string_view label) {
  std::error_code ec;
  if (std::filesystem::is_directory(path, ec)) {
    throw std::runtime_error("merge: " + std::string(label) +
                             " path is a directory, not a CSV file: " + path);
  }
}

// For merge --create: the temporary sibling is written next to the new store,
// so its parent directory must already exist. Directories are never created.
void RequireParentDirectory(const std::filesystem::path& store_path) {
  const auto parent = store_path.parent_path();
  if (parent.empty()) {
    return;  // relative file name in the current directory
  }
  std::error_code ec;
  if (!std::filesystem::is_directory(parent, ec)) {
    throw std::runtime_error("merge: parent directory does not exist: " + parent.string());
  }
}

}  // namespace

int RunAppend(const std::string& existing_path,
              const std::string& incoming_path,
              bool in_place,
              std::ostream& output,
              const LoggerCallbacks& logger,
              RunStats& stats) {
  std::filesystem::path temp_path;
  try {
    if (existing_path.empty() || incoming_path.empty()) {
      throw std::runtime_error("append requires existing and incoming CSV paths");
    }
    if (existing_path == "-" || incoming_path == "-") {
      throw std::runtime_error("append requires file paths; stdin is not supported in v1");
    }

    const auto existing_headers = HeadersFor(existing_path);
    const auto incoming_headers = HeadersFor(incoming_path);
    ValidateHeadersMatch(existing_headers, incoming_headers);

    if (!in_place) {
      WriteConcatenated(existing_path, incoming_path, existing_headers, output, stats);
      return 0;
    }

    const std::filesystem::path existing_fs_path(existing_path);
    temp_path = TempSiblingFor(existing_fs_path);
    {
      std::ofstream temp(temp_path, std::ios::binary | std::ios::trunc);
      if (!temp.is_open()) {
        throw std::runtime_error("append: unable to open temporary file: " + temp_path.string());
      }
      WriteConcatenated(existing_path, incoming_path, existing_headers, temp, stats);
      temp.close();
      if (!temp) {
        throw std::runtime_error("append: failed writing temporary file: " + temp_path.string());
      }
    }

    ReplaceFileWith(temp_path, existing_fs_path, "append");
    return 0;
  } catch (const std::exception& ex) {
    if (!temp_path.empty()) {
      std::error_code ec;
      std::filesystem::remove(temp_path, ec);
    }
    if (logger.error) {
      logger.error(ex.what());
    }
    return 1;
  }
}

int RunMerge(const std::string& existing_path,
             const std::string& incoming_path,
             const std::string& key_column,
             bool in_place,
             bool create_missing,
             std::ostream& output,
             const LoggerCallbacks& logger,
             RunStats& stats) {
  std::filesystem::path temp_path;
  try {
    if (existing_path.empty() || incoming_path.empty()) {
      throw std::runtime_error("merge requires existing and incoming CSV paths");
    }
    if (existing_path == "-" || incoming_path == "-") {
      throw std::runtime_error("merge requires file paths; stdin is not supported in v1");
    }
    if (key_column.empty()) {
      throw std::runtime_error("merge: --key is required");
    }

    // --create treats a nonexistent existing file as an empty store with the
    // incoming header. Every other case (including a directory or unreadable
    // file at that path) goes through the normal existing-file validation.
    const bool existing_present = !(create_missing && PathDoesNotExist(existing_path));
    if (existing_present) {
      RejectDirectory(existing_path, "existing");
    } else if (in_place) {
      RequireParentDirectory(existing_path);
    }

    // Store present: read existing headers before incoming (unchanged order and
    // diagnostics). Store missing: only incoming is read.
    const auto existing_headers =
        existing_present ? HeadersFor(existing_path) : HeadersFor(incoming_path);
    const auto incoming_headers =
        existing_present ? HeadersFor(incoming_path) : existing_headers;
    ValidateHeadersMatch(existing_headers, incoming_headers);

    const auto existing_keys = existing_present
                                   ? ReadExistingKeys(existing_path, existing_headers, key_column)
                                   : std::unordered_set<std::string>{};
    const std::size_t skipped_count =
        ValidateIncomingKeys(incoming_path, incoming_headers, key_column, existing_keys);

    const auto log_counts = [&](std::size_t added_count) {
      if (!logger.info) {
        return;
      }
      std::string prefix = "merge: ";
      if (!existing_present) {
        prefix += in_place ? "created " + existing_path + " from incoming; "
                           : existing_path + " does not exist; treated as empty; ";
      }
      logger.info(prefix + "added " + std::to_string(added_count) +
                  " row(s), skipped " + std::to_string(skipped_count) + " row(s)");
    };

    std::size_t added_count = 0;
    if (!in_place) {
      added_count = WriteMerged(existing_path, existing_present, incoming_path, existing_headers,
                                key_column, existing_keys, output, stats);
      log_counts(added_count);
      return 0;
    }

    const std::filesystem::path existing_fs_path(existing_path);
    temp_path = TempSiblingFor(existing_fs_path);
    {
      std::ofstream temp(temp_path, std::ios::binary | std::ios::trunc);
      if (!temp.is_open()) {
        throw std::runtime_error("merge: unable to open temporary file: " + temp_path.string());
      }
      added_count = WriteMerged(existing_path, existing_present, incoming_path, existing_headers,
                                key_column, existing_keys, temp, stats);
      temp.close();
      if (!temp) {
        throw std::runtime_error("merge: failed writing temporary file: " + temp_path.string());
      }
    }

    if (existing_present) {
      ReplaceFileWith(temp_path, existing_fs_path, "merge");
    } else {
      // The store was missing when checked above; never overwrite a store that
      // another process created while this run was writing.
      try {
        MoveIntoPlaceNoReplace(temp_path, existing_fs_path, "merge");
      } catch (const common::TargetExistsError&) {
        throw std::runtime_error("merge: " + existing_path +
                                 " appeared while --create was running; rerun to merge into it");
      }
    }
    log_counts(added_count);
    return 0;
  } catch (const std::exception& ex) {
    if (!temp_path.empty()) {
      std::error_code ec;
      std::filesystem::remove(temp_path, ec);
    }
    if (logger.error) {
      logger.error(ex.what());
    }
    return 1;
  }
}

}  // namespace csvzall::pipeline::commands
