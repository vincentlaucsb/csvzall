#pragma once

#include <optional>
#include <string>
#include <vector>

namespace csvzall {

struct CredentialTarget {
  std::string host = "localhost";
  int port = 5432;
  std::string database;
  std::string user;
};

class CredentialManager {
 public:
  [[nodiscard]] bool available() const;
  [[nodiscard]] std::string unavailable_reason() const;

  [[nodiscard]] std::string target_name(const CredentialTarget& target) const;
  [[nodiscard]] std::optional<std::string> get_password(const CredentialTarget& target,
                                                        std::string* error_message = nullptr) const;
  [[nodiscard]] bool save_password(const CredentialTarget& target,
                                   const std::string& password,
                                   std::string* error_message = nullptr) const;
  [[nodiscard]] bool forget_password(const CredentialTarget& target,
                                     std::string* error_message = nullptr) const;
  [[nodiscard]] std::size_t forget_all_postgres(std::string* error_message = nullptr) const;

 private:
  [[nodiscard]] std::vector<std::string> read_index(std::string* error_message = nullptr) const;
  [[nodiscard]] bool write_index(const std::vector<std::string>& targets,
                                 std::string* error_message = nullptr) const;
};

}  // namespace csvzall
