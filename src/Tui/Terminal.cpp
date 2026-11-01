#include "Terminal.hpp"

#include <string_view>

namespace termify::tui {

Terminal::Terminal(int inputFd, int outputFd)
    : _driver(platform::CurrentPlatform().CreateTerminal(inputFd, outputFd)) {}

Terminal::~Terminal() { Leave(); }

bool Terminal::Enter() {
  return _driver->Enter();
}

void Terminal::Leave() { _driver->Leave(); }

bool Terminal::Interactive() const { return _driver->Interactive(); }

std::optional<Event> Terminal::ReadEvent() {
  while (true) {
    if (auto event = _decoder.Pop())
      return event;
    if (_decoder.HasPendingBytes() && !WaitForSequence()) {
      _decoder.FlushPending();
      if (auto event = _decoder.Pop())
        return event;
    }

    const auto input = _driver->Read(64);
    if (input.status == platform::InputStatus::EndOfInput) {
      return Event{KeyEvent{Key::EndOfInput, {}, {}}};
    }
    if (input.status == platform::InputStatus::Interrupted) {
      return Event{KeyEvent{Key::Interrupt, {}, {.control = true}}};
    }
    if (input.status == platform::InputStatus::Retry) {
      continue;
    }

    _decoder.Feed(std::string_view(
        reinterpret_cast<const char *>(input.bytes.data()), input.bytes.size()));
    if (auto event = _decoder.Pop())
      return event;
    if (_decoder.HasPendingBytes() && !WaitForSequence()) {
      _decoder.FlushPending();
    }
  }
}

bool Terminal::WaitForSequence() const {
  return _driver->WaitForInput(std::chrono::milliseconds(25));
}

platform::TerminalSize Terminal::Size() const { return _driver->Size(); }

} // namespace termify::tui
