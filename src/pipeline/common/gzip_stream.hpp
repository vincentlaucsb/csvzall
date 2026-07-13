#pragma once

#include <array>
#include <istream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>

#include <csv.hpp>
#ifdef CSVZALL_HAVE_COMPRESSED_INPUT
#include <zlib.h>
#endif

namespace csvzall::pipeline::common {

bool IsGzipPath(std::string_view path);
bool IsZipPath(std::string_view path);
std::string ReadZipEntry(const std::string& path, const std::string& requested_entry);

#ifdef CSVZALL_HAVE_COMPRESSED_INPUT
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
#endif

template <typename... Args>
csv::CSVReader OpenCsvReader(const std::string& path,
                             const std::string& zip_entry,
                             Args&&... args) {
#ifndef CSVZALL_HAVE_COMPRESSED_INPUT
  (void)zip_entry;
#endif
  if (IsGzipPath(path)) {
#ifdef CSVZALL_HAVE_COMPRESSED_INPUT
    std::unique_ptr<std::istream> stream(new GzipInputStream(path));
    return csv::CSVReader(std::move(stream), std::forward<Args>(args)...);
#else
    throw std::runtime_error("gzip CSV input is disabled in this build");
#endif
  }
  if (IsZipPath(path)) {
#ifdef CSVZALL_HAVE_COMPRESSED_INPUT
    std::unique_ptr<std::istream> stream(new std::istringstream(ReadZipEntry(path, zip_entry)));
    return csv::CSVReader(std::move(stream), std::forward<Args>(args)...);
#else
    throw std::runtime_error("ZIP CSV input is disabled in this build");
#endif
  }

  return csv::CSVReader(path, std::forward<Args>(args)...);
}

template <typename... Args>
csv::CSVReader OpenCsvReader(const std::string& path, Args&&... args) {
  return OpenCsvReader(path, std::string{}, std::forward<Args>(args)...);
}

}  // namespace csvzall::pipeline::common
