#pragma once

#include "../../pipeline_types.hpp"

#include <csv.hpp>

#include <optional>
#include <memory>
#include <ostream>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace csvzall::pipeline::commands {

struct CsvRowIndexEntry {
  std::uint64_t byte_offset = 0;
};

struct CsvColumnDescriptor {
  std::string name;
  std::optional<std::size_t> source_index;
  std::string default_value;
};

struct CsvLogicalRow {
  std::optional<std::uint64_t> source_row;
  std::unordered_map<std::string, std::string> cells;
};

class CsvIndexedFile {
 public:
  static CsvIndexedFile Open(const std::string& input_path,
                             const RunOptions& options,
                             const LoggerCallbacks& logger,
                             RunStats& stats);

  [[nodiscard]] const std::string& input_path() const;
  [[nodiscard]] const std::string& file_name() const;
  [[nodiscard]] const std::vector<std::string>& headers() const;
  [[nodiscard]] std::uint64_t row_count() const;

  [[nodiscard]] std::vector<std::vector<std::string>> read_rows(
      std::uint64_t offset,
      std::uint64_t limit) const;

  void edit_cell(std::uint64_t row, const std::string& column, const std::string& value);
  void delete_row(std::uint64_t row);
  void insert_row(std::uint64_t row, const std::vector<std::string>& values);
  void swap_rows(std::uint64_t first, std::uint64_t second);
  void insert_column(std::uint64_t column, const std::string& name, const std::string& value);
  void rename_column(const std::string& column, const std::string& name);
  void delete_column(const std::string& column);
  bool recover_renamed_source();
  void reset();
  void save(const std::vector<std::string>& columns = {});

 private:
  [[nodiscard]] std::vector<std::vector<std::string>> read_source_rows(
      std::uint64_t offset,
      std::uint64_t limit) const;
  [[nodiscard]] std::vector<std::string> read_source_row(std::uint64_t source_row) const;
  [[nodiscard]] std::vector<std::string> row_values_for_columns(
      const CsvLogicalRow& row,
      const std::vector<CsvColumnDescriptor>& columns) const;
  [[nodiscard]] std::size_t column_index(const std::string& column) const;
  [[nodiscard]] bool has_column(const std::string& column) const;
  void refresh_headers();
  void validate_column_order(const std::vector<std::string>& columns) const;
  [[nodiscard]] std::vector<CsvColumnDescriptor> ordered_columns(
      const std::vector<std::string>& columns) const;
  void reload();

  std::string input_path_;
  std::string file_name_;
  std::vector<std::string> headers_;
  std::vector<CsvColumnDescriptor> columns_;
  std::vector<CsvRowIndexEntry> index_;
  std::vector<CsvLogicalRow> rows_;
  std::uint64_t file_size_ = 0;
  std::filesystem::file_time_type source_mtime_{};
  csv::CSVFormat format_;
};

enum class CsvViewDataMode {
  Paged,
};

class CsvViewData {
 public:
  static CsvViewData Open(const std::string& input_path,
                          const RunOptions& options,
                          const LoggerCallbacks& logger,
                          RunStats& stats);

  [[nodiscard]] CsvViewDataMode mode() const;
  [[nodiscard]] std::string_view mode_name() const;
  [[nodiscard]] const std::string& input_path() const;
  [[nodiscard]] const std::string& file_name() const;
  [[nodiscard]] const std::vector<std::string>& headers() const;
  [[nodiscard]] std::uint64_t row_count() const;

  [[nodiscard]] std::vector<std::vector<std::string>> read_rows(
      std::uint64_t offset,
      std::uint64_t limit) const;

  void edit_cell(std::uint64_t row, const std::string& column, const std::string& value);
  void delete_row(std::uint64_t row);
  void insert_row(std::uint64_t row, const std::vector<std::string>& values);
  void swap_rows(std::uint64_t first, std::uint64_t second);
  void insert_column(std::uint64_t column, const std::string& name, const std::string& value);
  void rename_column(const std::string& column, const std::string& name);
  void delete_column(const std::string& column);
  bool recover_renamed_source();
  void reset();
  void save(const std::vector<std::string>& columns = {});

 private:
  explicit CsvViewData(CsvIndexedFile indexed);

  CsvIndexedFile data_;
};

struct ViewServerOptions {
  int requested_port = 0;
  bool serve_once = false;
  bool editable = false;
  std::string session_token;
  std::string viewer_asset_dir;
};

class ViewServer {
 public:
  ViewServer(const CsvViewData& data, const LoggerCallbacks& logger);
  ~ViewServer();

  ViewServer(const ViewServer&) = delete;
  ViewServer& operator=(const ViewServer&) = delete;

  int Start(const ViewServerOptions& options = {});
  void Stop();
  int Wait();

  [[nodiscard]] int bound_port() const;
  [[nodiscard]] const std::string& session_token() const;
  [[nodiscard]] std::string viewer_url() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

std::string FormatViewStartupOutput(const std::string& url, bool startup_json);

int RunView(const std::string& input_path,
            std::ostream& output,
            const RunOptions& options,
            const LoggerCallbacks& logger,
            RunStats& stats,
            int requested_port,
            bool open_browser,
            bool serve_once,
            bool startup_json);

}  // namespace csvzall::pipeline::commands
