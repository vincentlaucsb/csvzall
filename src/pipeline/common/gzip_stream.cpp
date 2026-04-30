#include "gzip_stream.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace csvzall::pipeline::common {

bool IsGzipPath(std::string_view path) {
  constexpr std::string_view suffix = ".gz";
  if (path.size() < suffix.size()) {
    return false;
  }

  const auto start = path.size() - suffix.size();
  for (std::size_t i = 0; i < suffix.size(); ++i) {
    const auto lhs = static_cast<unsigned char>(path[start + i]);
    const auto rhs = static_cast<unsigned char>(suffix[i]);
    if (std::tolower(lhs) != std::tolower(rhs)) {
      return false;
    }
  }
  return true;
}

GzipStreambuf::GzipStreambuf(const std::string& path) {
  file_ = gzopen(path.c_str(), "rb");
  if (!file_) {
    throw std::runtime_error("Unable to open gzip file: " + path);
  }
  setg(buffer_.data(), buffer_.data(), buffer_.data());
}

GzipStreambuf::~GzipStreambuf() {
  if (file_) {
    gzclose(file_);
  }
}

GzipStreambuf::int_type GzipStreambuf::underflow() {
  if (!file_) {
    return traits_type::eof();
  }

  const int bytes = gzread(file_, buffer_.data(), static_cast<unsigned int>(buffer_.size()));
  if (bytes < 0) {
    int error_code = Z_OK;
    const char* message = gzerror(file_, &error_code);
    throw std::runtime_error(message ? message : "gzip decompression failed");
  }
  if (bytes == 0) {
    return traits_type::eof();
  }

  setg(buffer_.data(), buffer_.data(), buffer_.data() + bytes);
  return traits_type::to_int_type(*gptr());
}

GzipInputStream::GzipInputStream(const std::string& path)
    : std::istream(nullptr), buffer_(path) {
  rdbuf(&buffer_);
}

}  // namespace csvzall::pipeline::common
