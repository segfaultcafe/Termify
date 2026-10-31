#ifndef TERMIFY_TUI_EVENT_HPP
#define TERMIFY_TUI_EVENT_HPP

#include <string>
#include <variant>

namespace termify::tui {

enum class Key {
  Character,
  Enter,
  Escape,
  Backspace,
  Delete,
  Tab,
  BackTab,
  ArrowUp,
  ArrowDown,
  ArrowLeft,
  ArrowRight,
  Home,
  End,
  PageUp,
  PageDown,
  Interrupt,
  EndOfInput,
  Unknown,
};

struct Modifiers {
  bool shift{false};
  bool alt{false};
  bool control{false};

  bool operator==(const Modifiers &) const = default;
};

struct KeyEvent {
  Key key{Key::Unknown};
  std::string text;
  Modifiers modifiers;

  bool operator==(const KeyEvent &) const = default;
};

enum class MouseButton {
  None,
  Left,
  Middle,
  Right,
  WheelUp,
  WheelDown,
};

enum class MouseAction {
  Press,
  Release,
  Move,
  Scroll,
};

struct MouseEvent {
  MouseButton button{MouseButton::None};
  MouseAction action{MouseAction::Press};
  int column{0};
  int row{0};
  Modifiers modifiers;

  bool operator==(const MouseEvent &) const = default;
};

struct ResizeEvent {
  int columns{0};
  int rows{0};

  bool operator==(const ResizeEvent &) const = default;
};

using Event = std::variant<KeyEvent, MouseEvent, ResizeEvent>;

} // namespace termify::tui

#endif // TERMIFY_TUI_EVENT_HPP
