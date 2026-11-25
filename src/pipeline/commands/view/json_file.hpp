#pragma once

#include <filesystem>
#include <string>

namespace csvzall::pipeline::commands::view_internal {

class JsonFile {
 public:
  explicit JsonFile(const std::filesystem::path& path);
  std::string Document(bool editable);
  std::string Save(const std::string& revision, const std::string& text);

 private:
  std::filesystem::path path_;
  std::string source_;
  std::string revision_;
};

}  // namespace csvzall::pipeline::commands::view_internal
