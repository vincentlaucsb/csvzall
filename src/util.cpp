#include "util.hpp"

#include <cstdio>
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#include <conio.h>
#include <io.h>
#else
#include <termios.h>
#include <unistd.h>
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

}  // namespace csvzall::util
