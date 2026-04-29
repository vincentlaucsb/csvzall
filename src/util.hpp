#pragma once

#include <string>

namespace csvzall::util {

bool stdin_is_terminal();
std::string read_password(const std::string& prompt);

}  // namespace csvzall::util
