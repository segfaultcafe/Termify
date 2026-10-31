#ifndef TERMIFY_TUI_TERMINAL_HPP
#define TERMIFY_TUI_TERMINAL_HPP

#include "Event.hpp"
#include "InputDecoder.hpp"

#include <optional>
#include <termios.h>

namespace termify::tui {

class Terminal {
public:
  Terminal(int inputFd = 0, int outputFd = 1);
  ~Terminal();

  Terminal(const Terminal &) = delete;
  Terminal &operator=(const Terminal &) = delete;

  bool Enter();
  void Leave();
  bool Interactive() const;
  std::optional<Event> ReadEvent();

private:
  bool WaitForSequence() const;

  int _inputFd;
  int _outputFd;
  bool _interactive{false};
  bool _active{false};
  bool _termiosActive{false};
  termios _originalTermios{};
  InputDecoder _decoder;
};

} // namespace termify::tui

#endif // TERMIFY_TUI_TERMINAL_HPP
