#include "Tui/InputDecoder.hpp"
#include "Tui/Widget.hpp"
#include "Tui/Widgets.hpp"

#include <iostream>

using namespace termify::tui;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "Check failed at line " << __LINE__ << ": " #condition  \
                << '\n';                                                       \
      return 1;                                                                \
    }                                                                          \
  } while (false)

namespace {
class FocusTarget : public Widget {
public:
  explicit FocusTarget(std::string id) : Widget(std::move(id)) {}
  bool Focusable() const override { return true; }
  void Render(Canvas &) const override {}
  bool Handle(const Event &event) override {
    if (const auto *mouse = std::get_if<MouseEvent>(&event)) {
      clicked = mouse->action == MouseAction::Press;
      return clicked;
    }
    return false;
  }

  bool clicked{false};
};

std::optional<KeyEvent> nextKey(InputDecoder &decoder) {
  const auto event = decoder.Pop();
  if (!event)
    return std::nullopt;
  if (const auto *key = std::get_if<KeyEvent>(&*event))
    return *key;
  return std::nullopt;
}
} // namespace

int main() {
  InputDecoder decoder;
  decoder.Feed("\033[A\033[B\033[C\033[D");
  CHECK(nextKey(decoder)->key == Key::ArrowUp);
  CHECK(nextKey(decoder)->key == Key::ArrowDown);
  CHECK(nextKey(decoder)->key == Key::ArrowRight);
  CHECK(nextKey(decoder)->key == Key::ArrowLeft);

  decoder.Feed("\033[1;5C");
  const auto controlRight = nextKey(decoder);
  CHECK(controlRight && controlRight->key == Key::ArrowRight);
  CHECK(controlRight->modifiers.control);

  decoder.Feed("\033OD\033x");
  CHECK(nextKey(decoder)->key == Key::ArrowLeft);
  const auto altCharacter = nextKey(decoder);
  CHECK(altCharacter && altCharacter->key == Key::Character);
  CHECK(altCharacter->text == "x" && altCharacter->modifiers.alt);

  decoder.Feed("\033[<0;12");
  CHECK(!decoder.Pop());
  decoder.Feed(";7M");
  auto event = decoder.Pop();
  CHECK(event && std::holds_alternative<MouseEvent>(*event));
  const auto click = std::get<MouseEvent>(*event);
  CHECK(click.button == MouseButton::Left);
  CHECK(click.action == MouseAction::Press);
  CHECK(click.column == 12 && click.row == 7);

  decoder.Feed("\033[<65;2;3M");
  event = decoder.Pop();
  CHECK(event && std::get<MouseEvent>(*event).button == MouseButton::WheelDown);
  CHECK(std::get<MouseEvent>(*event).action == MouseAction::Scroll);

  decoder.Feed("é");
  const auto unicode = nextKey(decoder);
  CHECK(unicode && unicode->key == Key::Character && unicode->text == "é");

  decoder.Feed("\033");
  CHECK(!decoder.Pop());
  decoder.FlushPending();
  CHECK(nextKey(decoder)->key == Key::Escape);

  WidgetHost host;
  auto &left = host.Emplace<FocusTarget>("left");
  auto &right = host.Emplace<FocusTarget>("right");
  auto &down = host.Emplace<FocusTarget>("down");
  left.SetBounds({0, 0, 10, 2});
  right.SetBounds({20, 0, 10, 2});
  down.SetBounds({0, 5, 10, 2});
  CHECK(host.Focus("left"));
  CHECK(host.Dispatch(KeyEvent{Key::ArrowRight, {}, {}}));
  CHECK(host.FocusedId() == "right");
  CHECK(host.Focus("left"));
  CHECK(host.Dispatch(KeyEvent{Key::ArrowDown, {}, {}}));
  CHECK(host.FocusedId() == "down");
  CHECK(host.Dispatch(
      MouseEvent{MouseButton::Left, MouseAction::Press, 22, 1, {}}));
  CHECK(host.FocusedId() == "right");
  CHECK(right.clicked);

  TextInput input("input");
  input.SetBounds({0, 0, 30, 1});
  input.SetPrompt({{"termify › ", {Color::Cyan}}});
  CHECK(input.Handle(KeyEvent{Key::Character, "h", {}}));
  CHECK(input.Handle(KeyEvent{Key::Character, "é", {}}));
  CHECK(input.Handle(KeyEvent{Key::ArrowLeft, {}, {}}));
  CHECK(input.Handle(KeyEvent{Key::Character, "i", {}}));
  CHECK(input.Value() == "hié");
  CHECK(input.Handle(KeyEvent{Key::Enter, {}, {}}));
  CHECK(input.TakeSubmission() == "hié");
  CHECK(input.Value().empty());

  WidgetHost inputHost;
  auto &longInput = inputHost.Emplace<TextInput>("long-input");
  longInput.SetBounds({0, 0, 12, 1});
  longInput.SetPrompt({{"t › ", {Color::Cyan}}});
  CHECK(inputHost.Focus("long-input"));
  for (char character : std::string("abcdefghijklmnop")) {
    CHECK(inputHost.Dispatch(
        KeyEvent{Key::Character, std::string(1, character), {}}));
  }
  const auto inputCursor = inputHost.Cursor();
  CHECK(inputCursor && inputCursor->column >= 0 && inputCursor->column < 12);
  Canvas inputCanvas(12, 1);
  inputHost.Render(inputCanvas);

  Canvas canvas(8, 2);
  canvas.Text({0, 0}, "abcdefghij", {Color::Green});
  CHECK(Canvas::TextWidth("éx") == 2);
  CHECK(Canvas::Clip("abcdefgh", 6) == "abc...");

  return 0;
}
