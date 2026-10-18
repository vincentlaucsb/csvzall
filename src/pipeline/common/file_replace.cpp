#include "file_replace.hpp"

#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace csvzall::pipeline::common {
namespace {

[[noreturn]] void ThrowUnableToWrite(const std::filesystem::path& target_path,
                                     std::string_view label) {
  throw std::runtime_error(std::string(label) + ": unable to write " + target_path.string());
}

[[noreturn]] void ThrowTargetExists(const std::filesystem::path& target_path,
                                    std::string_view label) {
  throw TargetExistsError(std::string(label) + ": " + target_path.string() +
                          " already exists; refusing to overwrite it");
}

}  // namespace

std::filesystem::path TempSiblingFor(const std::filesystem::path& target_path) {
  return target_path.parent_path() / (target_path.filename().string() + ".csvzall.tmp");
}

void ReplaceFileWith(const std::filesystem::path& temp_path,
                     const std::filesystem::path& target_path,
                     std::string_view label) {
#ifdef _WIN32
  if (!MoveFileExW(temp_path.wstring().c_str(),
                   target_path.wstring().c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    ThrowUnableToWrite(target_path, label);
  }
#else
  std::error_code ec;
  std::filesystem::rename(temp_path, target_path, ec);
  if (ec) {
    ThrowUnableToWrite(target_path, label);
  }
#endif
}

void MoveIntoPlaceNoReplace(const std::filesystem::path& temp_path,
                            const std::filesystem::path& target_path,
                            std::string_view label) {
#ifdef _WIN32
  // Without MOVEFILE_REPLACE_EXISTING the move fails if the target exists.
  if (!MoveFileExW(temp_path.wstring().c_str(),
                   target_path.wstring().c_str(),
                   MOVEFILE_WRITE_THROUGH)) {
    const DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) {
      ThrowTargetExists(target_path, label);
    }
    ThrowUnableToWrite(target_path, label);
  }
#else
  // link() atomically fails with EEXIST instead of replacing the target.
  if (::link(temp_path.c_str(), target_path.c_str()) != 0) {
    if (errno == EEXIST) {
      ThrowTargetExists(target_path, label);
    }
    ThrowUnableToWrite(target_path, label);
  }
  std::error_code ec;
  std::filesystem::remove(temp_path, ec);
#endif
}

}  // namespace csvzall::pipeline::common
