#pragma once

#include "../../pipeline_types.hpp"

#include <csv.hpp>

#include <memory>
#include <ostream>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace csvzall::pipeline::commands {

struct CsvRowIndexEntry {
  std::uint64_t byte_offset = 0;
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

 private:
  std::string input_path_;
  std::string file_name_;
  std::vector<std::string> headers_;
  std::vector<CsvRowIndexEntry> index_;
  std::uint64_t file_size_ = 0;
  csv::CSVFormat format_;
};

struct CsvMaterializedFile {
  std::string input_path;
  std::string file_name;
  std::shared_ptr<csv::DataFrame<>> frame;
  csv::CSVFormat format;
  std::uint64_t source_size = 0;
  std::filesystem::file_time_type source_mtime{};
};

enum class CsvViewDataMode {
  Materialized,
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
  explicit CsvViewData(CsvMaterializedFile materialized);
  explicit CsvViewData(CsvIndexedFile indexed);

  std::variant<CsvMaterializedFile, CsvIndexedFile> data_;
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
