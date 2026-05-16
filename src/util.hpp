#pragma once

#include <filesystem>
#include <string>

namespace csvzall::util {

bool stdin_is_terminal();
std::string read_password(const std::string& prompt);
std::filesystem::path executable_path();

}  // namespace csvzall::util
