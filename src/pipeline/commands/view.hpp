#pragma once

#include "../../pipeline_types.hpp"
#include "../../viewer_core/indexed_csv.hpp"

#include <memory>
#include <ostream>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace csvzall::pipeline::commands {

using CsvRowIndexEntry = ::csvzall::viewer::CsvRowIndexEntry;
using CsvColumnDescriptor = ::csvzall::viewer::CsvColumnDescriptor;
using CsvLogicalRow = ::csvzall::viewer::CsvLogicalRow;
using CsvViewDataMode = ::csvzall::viewer::CsvViewDataMode;

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
  [[nodiscard]] std::uint64_t file_size() const;

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
  explicit CsvIndexedFile(::csvzall::viewer::CsvIndexedFile indexed);

  ::csvzall::viewer::CsvIndexedFile data_;
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
  [[nodiscard]] std::uint64_t file_size() const;
  [[nodiscard]] std::string delimiter() const;
  [[nodiscard]] std::string quote() const;
  [[nodiscard]] bool has_headers() const;
  [[nodiscard]] bool has_bom() const;
  void set_csv_options(const std::string& delimiter, const std::string& quote, bool has_headers);
  void read_csv_options(const std::string& delimiter, const std::string& quote, bool has_headers);

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
  explicit CsvViewData(::csvzall::viewer::CsvViewData data);

  ::csvzall::viewer::CsvViewData data_;
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
