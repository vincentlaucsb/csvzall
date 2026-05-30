#include "util.hpp"

#include <cstdio>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#include <Windows.h>
#include <conio.h>
#include <io.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif !defined(_WIN32)
#include <limits.h>
#endif

namespace csvzall::util {

bool stdin_is_terminal() {
#ifdef _WIN32
  return _isatty(_fileno(stdin)) != 0;
#else
  return isatty(STDIN_FILENO) != 0;
#endif
}

std::string read_password(const std::string& prompt) {
  std::cerr << prompt;

  std::string password;

#ifdef _WIN32
  while (true) {
    const int ch = _getch();
    if (ch == '\r' || ch == '\n') {
      break;
    }
    if (ch == 3) {
      throw std::runtime_error("password prompt interrupted");
    }
    if (ch == '\b') {
      if (!password.empty()) {
        password.pop_back();
      }
      continue;
    }
    if (ch == 0 || ch == 224) {
      (void)_getch();
      continue;
    }
    password.push_back(static_cast<char>(ch));
  }
#else
  termios old_term{};
  termios new_term{};
  const int fd = STDIN_FILENO;
  bool disabled_echo = false;

  if (isatty(fd) && tcgetattr(fd, &old_term) == 0) {
    new_term = old_term;
    new_term.c_lflag &= static_cast<tcflag_t>(~ECHO);
    disabled_echo = (tcsetattr(fd, TCSAFLUSH, &new_term) == 0);
  }

  std::getline(std::cin, password);

  if (disabled_echo) {
    tcsetattr(fd, TCSAFLUSH, &old_term);
  }
#endif

  std::cerr << '\n';
  return password;
}

std::filesystem::path executable_path() {
#ifdef _WIN32
  std::wstring buffer(MAX_PATH, L'\0');
  while (true) {
    const auto size = static_cast<DWORD>(buffer.size());
    const auto copied = GetModuleFileNameW(nullptr, buffer.data(), size);
    if (copied == 0) {
      throw std::runtime_error("unable to resolve executable path");
    }
    if (copied < size) {
      buffer.resize(copied);
      return std::filesystem::path(buffer);
    }
    buffer.resize(buffer.size() * 2);
  }
#elif defined(__APPLE__)
  std::uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    throw std::runtime_error("unable to resolve executable path");
  }
  return std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str()));
#else
  std::string buffer(PATH_MAX, '\0');
  const auto copied = readlink("/proc/self/exe", buffer.data(), buffer.size());
  if (copied < 0) {
    throw std::runtime_error("unable to resolve executable path");
  }
  buffer.resize(static_cast<std::size_t>(copied));
  return std::filesystem::path(buffer);
#endif
}

}  // namespace csvzall::util
