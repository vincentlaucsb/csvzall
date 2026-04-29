#include "credentials.hpp"

#include <algorithm>
#include <sstream>

#ifdef CSVZALL_HAVE_KEYCHAIN
#include <keychain/keychain.h>
#endif

namespace csvzall {

namespace {

constexpr const char* kPackage = "csvzall";
constexpr const char* kPasswordUser = "password";
constexpr const char* kIndexService = "csvzall:postgres:index";
constexpr const char* kIndexUser = "targets";

#ifdef CSVZALL_HAVE_KEYCHAIN
void SetError(std::string* out, const keychain::Error& error) {
  if (out) {
    *out = error.message;
  }
}
#else
void SetUnavailable(std::string* out) {
  if (out) {
    *out = "credential storage is not available in this build";
  }
}
#endif

void ClearError(std::string* out) {
  if (out) {
    out->clear();
  }
}

std::vector<std::string> SplitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream input(text);
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

std::string JoinLines(const std::vector<std::string>& lines) {
  std::string out;
  for (const auto& line : lines) {
    out += line;
    out.push_back('\n');
  }
  return out;
}

}  // namespace

bool CredentialManager::available() const {
#ifdef CSVZALL_HAVE_KEYCHAIN
  return true;
#else
  return false;
#endif
}

std::string CredentialManager::unavailable_reason() const {
#ifdef CSVZALL_HAVE_KEYCHAIN
  return {};
#else
  return "credential storage is not available in this build";
#endif
}

std::string CredentialManager::target_name(const CredentialTarget& target) const {
  return "csvzall:postgres:" + target.host + ":" + std::to_string(target.port) + ":" +
         target.database + ":" + target.user;
}

std::optional<std::string> CredentialManager::get_password(
    const CredentialTarget& target,
    std::string* error_message) const {
  ClearError(error_message);
#ifdef CSVZALL_HAVE_KEYCHAIN
  keychain::Error error;
  auto password = keychain::getPassword(kPackage, target_name(target), kPasswordUser, error);
  if (error.type == keychain::ErrorType::NotFound) {
    return std::nullopt;
  }
  if (error) {
    SetError(error_message, error);
    return std::nullopt;
  }
  return password;
#else
  (void)target;
  SetUnavailable(error_message);
  return std::nullopt;
#endif
}

bool CredentialManager::save_password(const CredentialTarget& target,
                                      const std::string& password,
                                      std::string* error_message) const {
  ClearError(error_message);
#ifdef CSVZALL_HAVE_KEYCHAIN
  const auto target_key = target_name(target);
  keychain::Error error;
  keychain::setPassword(kPackage, target_key, kPasswordUser, password, error);
  if (error) {
    SetError(error_message, error);
    return false;
  }

  auto targets = read_index(error_message);
  if (error_message && !error_message->empty()) {
    return false;
  }
  if (std::find(targets.begin(), targets.end(), target_key) == targets.end()) {
    targets.push_back(target_key);
    std::sort(targets.begin(), targets.end());
    return write_index(targets, error_message);
  }
  return true;
#else
  (void)target;
  (void)password;
  SetUnavailable(error_message);
  return false;
#endif
}

bool CredentialManager::forget_password(const CredentialTarget& target,
                                        std::string* error_message) const {
  ClearError(error_message);
#ifdef CSVZALL_HAVE_KEYCHAIN
  const auto target_key = target_name(target);
  keychain::Error error;
  keychain::deletePassword(kPackage, target_key, kPasswordUser, error);
  if (error && error.type != keychain::ErrorType::NotFound) {
    SetError(error_message, error);
    return false;
  }

  auto targets = read_index(nullptr);
  const auto old_size = targets.size();
  targets.erase(std::remove(targets.begin(), targets.end(), target_key), targets.end());
  if (targets.size() != old_size) {
    return write_index(targets, error_message);
  }
  return true;
#else
  (void)target;
  SetUnavailable(error_message);
  return false;
#endif
}

std::size_t CredentialManager::forget_all_postgres(std::string* error_message) const {
  ClearError(error_message);
#ifdef CSVZALL_HAVE_KEYCHAIN
  const auto targets = read_index(error_message);
  if (error_message && !error_message->empty()) {
    return 0;
  }

  std::size_t deleted = 0;
  for (const auto& target_key : targets) {
    keychain::Error error;
    keychain::deletePassword(kPackage, target_key, kPasswordUser, error);
    if (error && error.type != keychain::ErrorType::NotFound) {
      SetError(error_message, error);
      return deleted;
    }
    if (error.type != keychain::ErrorType::NotFound) {
      deleted++;
    }
  }

  keychain::Error error;
  keychain::deletePassword(kPackage, kIndexService, kIndexUser, error);
  if (error && error.type != keychain::ErrorType::NotFound) {
    SetError(error_message, error);
  }
  return deleted;
#else
  SetUnavailable(error_message);
  return 0;
#endif
}

std::vector<std::string> CredentialManager::read_index(std::string* error_message) const {
  ClearError(error_message);
#ifdef CSVZALL_HAVE_KEYCHAIN
  keychain::Error error;
  auto index_text = keychain::getPassword(kPackage, kIndexService, kIndexUser, error);
  if (error.type == keychain::ErrorType::NotFound) {
    return {};
  }
  if (error) {
    SetError(error_message, error);
    return {};
  }
  return SplitLines(index_text);
#else
  SetUnavailable(error_message);
  return {};
#endif
}

bool CredentialManager::write_index(const std::vector<std::string>& targets,
                                    std::string* error_message) const {
  ClearError(error_message);
#ifdef CSVZALL_HAVE_KEYCHAIN
  keychain::Error error;
  keychain::setPassword(kPackage, kIndexService, kIndexUser, JoinLines(targets), error);
  if (error) {
    SetError(error_message, error);
    return false;
  }
  return true;
#else
  (void)targets;
  SetUnavailable(error_message);
  return false;
#endif
}

}  // namespace csvzall
