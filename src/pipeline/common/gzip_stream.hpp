#pragma once

#include <array>
#include <istream>
#include <memory>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>

#include <csv.hpp>
#include <zlib.h>

namespace csvzall::pipeline::common {

bool IsGzipPath(std::string_view path);

class GzipStreambuf : public std::streambuf {
 public:
  explicit GzipStreambuf(const std::string& path);
  ~GzipStreambuf() override;

  GzipStreambuf(const GzipStreambuf&) = delete;
  GzipStreambuf& operator=(const GzipStreambuf&) = delete;

 protected:
  int_type underflow() override;

 private:
  gzFile file_ = nullptr;
  std::array<char, 64 * 1024> buffer_{};
};

class GzipInputStream : public std::istream {
 public:
  explicit GzipInputStream(const std::string& path);

 private:
  GzipStreambuf buffer_;
};

template <typename... Args>
csv::CSVReader OpenCsvReader(const std::string& path, Args&&... args) {
  if (IsGzipPath(path)) {
    std::unique_ptr<std::istream> stream(new GzipInputStream(path));
    return csv::CSVReader(std::move(stream), std::forward<Args>(args)...);
  }

  return csv::CSVReader(path, std::forward<Args>(args)...);
}

}  // namespace csvzall::pipeline::common
