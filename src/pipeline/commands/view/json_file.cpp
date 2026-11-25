#include "json_file.hpp"
#include "support.hpp"
#include "../../common/file_replace.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <string_view>
#ifdef _WIN32
#include <windows.h>
#include <sddl.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <copyfile.h>
#else
#include <sys/xattr.h>
#endif
#endif

namespace csvzall::pipeline::commands::view_internal {
namespace {
#ifndef _WIN32
#ifndef __APPLE__
void PreserveUserAttributes(const std::filesystem::path& source, const std::filesystem::path& staged) {
  const auto size = listxattr(source.c_str(), nullptr, 0);
  if (size < 0 && errno == ENOTSUP) return;
  if (size < 0) throw std::runtime_error("Cannot list JSON file attributes");
  std::string names(static_cast<std::size_t>(size), '\0');
  if (listxattr(source.c_str(), names.data(), names.size()) != size)
    throw std::runtime_error("JSON file attributes changed during save");
  for (std::size_t offset = 0; offset < names.size();) {
    const char* name = names.data() + offset;
    const std::string_view attribute(name);
    offset += attribute.size() + 1;
    if (attribute.substr(0, 5) != "user.") continue;
    const auto length = getxattr(source.c_str(), name, nullptr, 0);
    if (length < 0) throw std::runtime_error("Cannot read JSON file attribute");
    std::string value(static_cast<std::size_t>(length), '\0');
    if (getxattr(source.c_str(), name, value.data(), value.size()) != length ||
        setxattr(staged.c_str(), name, value.data(), value.size(), 0) != 0)
      throw std::runtime_error("Cannot preserve JSON file attribute");
  }
}
#endif
void PreserveAccess(const std::filesystem::path& source, const std::filesystem::path& staged) {
  const int fd = open(source.c_str(), O_WRONLY | O_CLOEXEC);
  if (fd < 0) throw std::runtime_error("JSON file is not writable");
  struct stat access{};
  const int result = fstat(fd, &access);
  close(fd);
  if (result || chown(staged.c_str(), access.st_uid, access.st_gid) ||
      chmod(staged.c_str(), access.st_mode & 0777))
    throw std::runtime_error("Cannot preserve JSON file ownership or permissions");
#ifdef __APPLE__
  if (copyfile(source.c_str(), staged.c_str(), nullptr, COPYFILE_ACL | COPYFILE_XATTR) != 0)
    throw std::runtime_error("Cannot preserve JSON file ACL and attributes");
#else
  PreserveUserAttributes(source, staged);
  constexpr auto acl = "system.posix_acl_access";
  const auto size = getxattr(source.c_str(), acl, nullptr, 0);
  if (size < 0 && errno != ENODATA && errno != ENOTSUP)
    throw std::runtime_error("Cannot read JSON file ACL");
  if (size > 0) {
    std::string buffer(static_cast<std::size_t>(size), '\0');
    if (getxattr(source.c_str(), acl, buffer.data(), buffer.size()) != size ||
        setxattr(staged.c_str(), acl, buffer.data(), buffer.size(), 0) != 0)
      throw std::runtime_error("Cannot preserve JSON file ACL");
  } else if (removexattr(staged.c_str(), acl) != 0 && errno != ENODATA && errno != ENOTSUP) {
    throw std::runtime_error("Cannot remove inherited JSON staging ACL");
  }
#endif
}
#endif
std::string Read(const std::filesystem::path& path) {
  if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("JSON source is not a regular file");
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  const auto size = input.tellg();
  if (size < 0) throw std::runtime_error("Cannot read JSON source");
  std::string text(static_cast<std::size_t>(size), '\0');
  input.seekg(0);
  if (!input.read(text.data(), size) || input.peek() != std::char_traits<char>::eof() || input.bad())
    throw std::runtime_error("JSON source changed or could not be read");
  return text;
}

// Kernel-owned locks are released when a helper is terminated by its host.
struct SaveLock {
#ifdef _WIN32
  HANDLE handle;
  explicit SaveLock(std::filesystem::path target) {
    target += ".csvzall-json-lock";
    handle = CreateFileW(target.c_str(), DELETE, 0, nullptr, CREATE_NEW,
                         FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot lock JSON file for save");
  }
  ~SaveLock() { CloseHandle(handle); }
#else
  int fd;
  explicit SaveLock(const std::filesystem::path& target) {
    fd = open(target.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) throw std::runtime_error("Cannot open JSON parent folder for save");
    if (flock(fd, LOCK_EX) != 0) {
      close(fd);
      throw std::runtime_error("Cannot lock JSON folder for save");
    }
  }
  ~SaveLock() { close(fd); }
#endif
};

struct StagedSave {
  std::filesystem::path directory;
  std::filesystem::path file;
  explicit StagedSave(const std::filesystem::path& target)
      : directory(target.parent_path() / (".csvzall-save-" + GenerateSessionToken())), file(directory / "new") {
#ifdef _WIN32
    PSECURITY_DESCRIPTOR permissions = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;OW)", SDDL_REVISION_1,
                                                               &permissions, nullptr))
      throw std::runtime_error("Cannot secure JSON staging directory");
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), permissions, FALSE};
    const bool created = CreateDirectoryW(directory.c_str(), &security) != FALSE;
    LocalFree(permissions);
#else
    const bool created = mkdir(directory.c_str(), 0700) == 0;
#endif
    if (!created) throw std::runtime_error("Cannot create private JSON staging directory");
  }
  ~StagedSave() {
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
    std::filesystem::remove(directory, ignored);
  }
};
}  // namespace

JsonFile::JsonFile(const std::filesystem::path& path)
    : path_(std::filesystem::canonical(path)), source_(Read(path_)), revision_(GenerateSessionToken()) {}

std::string JsonFile::Document(bool editable) {
  auto text = Read(path_);
  if (text != source_) {
    source_ = std::move(text);
    revision_ = GenerateSessionToken();
  }
  const auto name = path_.filename().u8string();
  return nlohmann::json{{"name", std::string(name.begin(), name.end())}, {"text", source_},
                        {"editable", editable}, {"revision", revision_}}.dump();
}

std::string JsonFile::Save(const std::string& expected, const std::string& text) {
  if (expected != revision_) throw std::runtime_error("JSON file changed; copy your edits before reloading.");
  if (text == source_ && Read(path_) == source_) return nlohmann::json{{"revision", revision_}}.dump();
  StagedSave stage(path_);
  {
    std::ofstream output;
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output.open(stage.file, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
  }
  const SaveLock lock(path_);
#ifndef _WIN32
  PreserveAccess(path_, stage.file);
#endif
  if (Read(path_) != source_) throw std::runtime_error("JSON file changed on disk; copy your edits before reloading.");
#ifdef _WIN32
  if (!ReplaceFileW(path_.c_str(), stage.file.c_str(), nullptr, 0, nullptr, nullptr))
    throw std::runtime_error("Cannot replace JSON file with its original access controls");
#else
  common::ReplaceFileWith(stage.file, path_, "JSON save");
#endif
  source_ = text;
  revision_ = GenerateSessionToken();
  return nlohmann::json{{"revision", revision_}}.dump();
}
}  // namespace csvzall::pipeline::commands::view_internal
