#ifndef TERMIFY_TUI_TERMINAL_HPP
#define TERMIFY_TUI_TERMINAL_HPP

#include "Event.hpp"
#include "InputDecoder.hpp"
#include "../Platform/Services.hpp"

#include <memory>
#include <optional>

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
  platform::TerminalSize Size() const;

private:
  bool WaitForSequence() const;

  std::unique_ptr<platform::TerminalDriver> _driver;
  InputDecoder _decoder;
};

} // namespace termify::tui

#endif // TERMIFY_TUI_TERMINAL_HPP
