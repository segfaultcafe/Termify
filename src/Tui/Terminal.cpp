#include "Terminal.hpp"

#include <cerrno>
#include <poll.h>
#include <string_view>
#include <unistd.h>

namespace termify::tui {

namespace {
constexpr std::string_view ENTER_SEQUENCE =
    "\033[?1049h\033[2J\033[H\033[?1000h\033[?1003h\033[?1006h\033[?25h";
constexpr std::string_view LEAVE_SEQUENCE =
    "\033[?1006l\033[?1003l\033[?1000l\033[?25h\033[?1049l";

void writeAll(int descriptor, std::string_view value) {
  while (!value.empty()) {
    const ssize_t written = write(descriptor, value.data(), value.size());
    if (written > 0) {
      value.remove_prefix(static_cast<std::size_t>(written));
    } else if (written < 0 && errno == EINTR) {
      continue;
    } else {
      return;
    }
  }
}
} // namespace

Terminal::Terminal(int inputFd, int outputFd)
    : _inputFd(inputFd), _outputFd(outputFd),
      _interactive(isatty(inputFd) && isatty(outputFd)) {}

Terminal::~Terminal() { Leave(); }

bool Terminal::Enter() {
  if (_active)
    return _interactive;
  if (!_interactive)
    return false;

  if (tcgetattr(_inputFd, &_originalTermios) != 0) {
    _interactive = false;
    return false;
  }
  termios inputMode = _originalTermios;
  inputMode.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
  inputMode.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL));
  inputMode.c_cc[VMIN] = 1;
  inputMode.c_cc[VTIME] = 0;
  if (tcsetattr(_inputFd, TCSAFLUSH, &inputMode) != 0) {
    _interactive = false;
    return false;
  }
  _termiosActive = true;

  writeAll(_outputFd, ENTER_SEQUENCE);
  _active = true;
  return true;
}

void Terminal::Leave() {
  if (!_active)
    return;
  writeAll(_outputFd, LEAVE_SEQUENCE);
  if (_termiosActive) {
    tcsetattr(_inputFd, TCSAFLUSH, &_originalTermios);
    _termiosActive = false;
  }
  _active = false;
}

bool Terminal::Interactive() const { return _interactive; }

std::optional<Event> Terminal::ReadEvent() {
  while (true) {
    if (auto event = _decoder.Pop())
      return event;
    if (_decoder.HasPendingBytes() && !WaitForSequence()) {
      _decoder.FlushPending();
      if (auto event = _decoder.Pop())
        return event;
    }

    unsigned char bytes[64]{};
    const ssize_t count = read(_inputFd, bytes, sizeof(bytes));
    if (count == 0) {
      return Event{KeyEvent{Key::EndOfInput, {}, {}}};
    }
    if (count < 0) {
      if (errno == EINTR) {
        return Event{KeyEvent{Key::Interrupt, {}, {.control = true}}};
      }
      continue;
    }

    _decoder.Feed(std::string_view(reinterpret_cast<char *>(bytes),
                                   static_cast<std::size_t>(count)));
    if (auto event = _decoder.Pop())
      return event;
    if (_decoder.HasPendingBytes() && !WaitForSequence()) {
      _decoder.FlushPending();
    }
  }
}

bool Terminal::WaitForSequence() const {
  pollfd descriptor{.fd = _inputFd, .events = POLLIN, .revents = 0};
  int result = 0;
  do {
    result = poll(&descriptor, 1, 25);
  } while (result < 0 && errno == EINTR);
  return result > 0 && (descriptor.revents & POLLIN) != 0;
}

} // namespace termify::tui
