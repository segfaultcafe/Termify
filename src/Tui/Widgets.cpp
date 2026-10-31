#include "Widgets.hpp"

#include <algorithm>

namespace termify::tui {

Label::Label(std::string id) : Widget(std::move(id)) {}

void Label::SetText(std::string text, Style style) {
  _spans = {{std::move(text), style}};
}

void Label::SetSpans(std::vector<Span> spans) { _spans = std::move(spans); }
void Label::SetAlignment(Alignment alignment) { _alignment = alignment; }

void Label::Render(Canvas &canvas) const {
  std::size_t contentWidth = 0;
  for (const auto &span : _spans) {
    contentWidth += Canvas::TextWidth(span.text);
  }
  const auto &bounds = Bounds();
  int offset = 0;
  if (_alignment == Alignment::Center) {
    offset = std::max(0, (bounds.width - static_cast<int>(contentWidth)) / 2);
  } else if (_alignment == Alignment::Right) {
    offset = std::max(0, bounds.width - static_cast<int>(contentWidth));
  }
  canvas.Spans({bounds.column + offset, bounds.row}, _spans,
               bounds.width - offset);
}

Frame::Frame(std::string id) : Widget(std::move(id)) {}
void Frame::SetTitle(std::vector<Span> title) { _title = std::move(title); }
void Frame::SetStyle(Style style) { _style = style; }

void Frame::Render(Canvas &canvas) const {
  const auto &bounds = Bounds();
  if (bounds.width < 2 || bounds.height < 2)
    return;
  canvas.Put({bounds.column, bounds.row}, "╭", _style);
  canvas.Put({bounds.column + bounds.width - 1, bounds.row}, "╮", _style);
  canvas.Put({bounds.column, bounds.row + bounds.height - 1}, "╰", _style);
  canvas.Put({bounds.column + bounds.width - 1, bounds.row + bounds.height - 1},
             "╯", _style);
  canvas.HorizontalLine({bounds.column + 1, bounds.row}, bounds.width - 2, "─",
                        _style);
  canvas.HorizontalLine({bounds.column + 1, bounds.row + bounds.height - 1},
                        bounds.width - 2, "─", _style);
  canvas.VerticalLine({bounds.column, bounds.row + 1}, bounds.height - 2, "│",
                      _style);
  canvas.VerticalLine({bounds.column + bounds.width - 1, bounds.row + 1},
                      bounds.height - 2, "│", _style);
  if (!_title.empty()) {
    canvas.Text({bounds.column + 1, bounds.row}, "─", _style, 1);
    canvas.Spans({bounds.column + 2, bounds.row}, _title, bounds.width - 4);
    int titleWidth = 0;
    for (const auto &span : _title)
      titleWidth += static_cast<int>(Canvas::TextWidth(span.text));
    canvas.Text({bounds.column + 2 + titleWidth, bounds.row}, " ", _style, 1);
  }
}

Rule::Rule(std::string id) : Widget(std::move(id)) {}
void Rule::SetLabel(std::string label) { _label = std::move(label); }
void Rule::SetStyle(Style style) { _style = style; }

void Rule::Render(Canvas &canvas) const {
  const auto &bounds = Bounds();
  if (bounds.width < 2)
    return;
  canvas.Put({bounds.column, bounds.row}, "├", _style);
  canvas.Put({bounds.column + bounds.width - 1, bounds.row}, "┤", _style);
  canvas.HorizontalLine({bounds.column + 1, bounds.row}, bounds.width - 2, "─",
                        _style);
  if (!_label.empty() && bounds.width > 6) {
    const std::string label = "─ " + _label + " ";
    canvas.Text({bounds.column + 1, bounds.row}, label, _style,
                bounds.width - 2);
  }
}

ProgressBar::ProgressBar(std::string id) : Widget(std::move(id)) {}

void ProgressBar::SetProgress(double progress) {
  _progress = std::clamp(progress, 0.0, 1.0);
}

void ProgressBar::SetLabels(std::string leading, std::string trailing) {
  _leading = std::move(leading);
  _trailing = std::move(trailing);
}

void ProgressBar::Render(Canvas &canvas) const {
  const auto &bounds = Bounds();
  const int fixed = static_cast<int>(Canvas::TextWidth(_leading) +
                                     Canvas::TextWidth(_trailing)) +
                    4;
  const int barWidth = std::max(1, bounds.width - fixed);
  const int filled = static_cast<int>(_progress * barWidth);
  int column = bounds.column;
  column += canvas.Text({column, bounds.row}, _leading + " [", {Color::Muted},
                        bounds.width);
  column += canvas.Text({column, bounds.row}, std::string(filled, ' '),
                        {Color::Cyan}, barWidth);
  for (int index = 0; index < barWidth; ++index) {
    canvas.Put({column - filled + index, bounds.row},
               index < filled ? "━" : "─",
               {index < filled ? Color::Cyan : Color::Muted});
  }
  column = bounds.column + static_cast<int>(Canvas::TextWidth(_leading)) + 2 +
           barWidth;
  canvas.Text({column, bounds.row}, "] " + _trailing, {Color::Muted},
              bounds.column + bounds.width - column);
}

TextInput::TextInput(std::string id) : Widget(std::move(id)) {}
void TextInput::SetPrompt(std::vector<Span> prompt) {
  _prompt = std::move(prompt);
}
bool TextInput::Focusable() const { return true; }

bool TextInput::Handle(const Event &event) {
  if (const auto *mouse = std::get_if<MouseEvent>(&event)) {
    if (mouse->action == MouseAction::Press &&
        mouse->button == MouseButton::Left) {
      PlaceCursorFromMouse(mouse->column);
      return true;
    }
    return false;
  }
  const auto *key = std::get_if<KeyEvent>(&event);
  if (!key)
    return false;
  switch (key->key) {
  case Key::Character:
    _value.insert(_cursor, key->text);
    _cursor += key->text.size();
    return true;
  case Key::Backspace:
    if (_cursor > 0) {
      const auto previous = PreviousCharacter(_value, _cursor);
      _value.erase(previous, _cursor - previous);
      _cursor = previous;
    }
    return true;
  case Key::Delete:
    if (_cursor < _value.size()) {
      _value.erase(_cursor, NextCharacter(_value, _cursor) - _cursor);
    }
    return true;
  case Key::ArrowLeft:
    _cursor = PreviousCharacter(_value, _cursor);
    return true;
  case Key::ArrowRight:
    _cursor = NextCharacter(_value, _cursor);
    return true;
  case Key::Home:
    _cursor = 0;
    return true;
  case Key::End:
    _cursor = _value.size();
    return true;
  case Key::Enter:
    _submissions.push_back(std::move(_value));
    _value.clear();
    _cursor = 0;
    return true;
  default:
    return false;
  }
}

void TextInput::Render(Canvas &canvas) const {
  const auto &bounds = Bounds();
  const int promptWidth =
      canvas.Spans({bounds.column, bounds.row}, _prompt, bounds.width);
  const auto start = ByteOffsetForCharacter(_value, VisibleStart());
  canvas.Text({bounds.column + promptWidth, bounds.row},
              std::string_view(_value).substr(start), {},
              bounds.width - promptWidth);
}

std::optional<Point> TextInput::Cursor() const {
  if (!Focused())
    return std::nullopt;
  std::size_t promptWidth = 0;
  for (const auto &span : _prompt)
    promptWidth += Canvas::TextWidth(span.text);
  const auto visibleStart = VisibleStart();
  const int relativeColumn =
      static_cast<int>(promptWidth) +
      static_cast<int>(CharacterCount(_value, _cursor) - visibleStart);
  return Point{Bounds().column + std::clamp(relativeColumn, 0,
                                            std::max(0, Bounds().width - 1)),
               Bounds().row};
}

std::optional<std::string> TextInput::TakeSubmission() {
  if (_submissions.empty())
    return std::nullopt;
  auto submission = std::move(_submissions.front());
  _submissions.pop_front();
  return submission;
}

const std::string &TextInput::Value() const { return _value; }
void TextInput::Clear() {
  _value.clear();
  _cursor = 0;
  _submissions.clear();
}

std::size_t TextInput::PreviousCharacter(std::string_view text,
                                         std::size_t cursor) {
  if (cursor == 0)
    return 0;
  --cursor;
  while (cursor > 0 &&
         (static_cast<unsigned char>(text[cursor]) & 0xC0) == 0x80) {
    --cursor;
  }
  return cursor;
}

std::size_t TextInput::NextCharacter(std::string_view text,
                                     std::size_t cursor) {
  if (cursor >= text.size())
    return text.size();
  ++cursor;
  while (cursor < text.size() &&
         (static_cast<unsigned char>(text[cursor]) & 0xC0) == 0x80) {
    ++cursor;
  }
  return cursor;
}

std::size_t TextInput::CharacterCount(std::string_view text, std::size_t end) {
  end = std::min(end, text.size());
  return static_cast<std::size_t>(std::count_if(
      text.begin(), text.begin() + static_cast<std::ptrdiff_t>(end),
      [](unsigned char byte) { return (byte & 0xC0) != 0x80; }));
}

std::size_t TextInput::ByteOffsetForCharacter(std::string_view text,
                                              std::size_t character) {
  std::size_t offset = 0;
  while (character > 0 && offset < text.size()) {
    offset = NextCharacter(text, offset);
    --character;
  }
  return offset;
}

std::size_t TextInput::VisibleStart() const {
  std::size_t promptWidth = 0;
  for (const auto &span : _prompt)
    promptWidth += Canvas::TextWidth(span.text);
  const int available =
      Bounds().width - std::min(Bounds().width, static_cast<int>(promptWidth));
  if (available <= 0)
    return CharacterCount(_value, _cursor);
  const auto cursor = CharacterCount(_value, _cursor);
  const auto width = static_cast<std::size_t>(available);
  return cursor >= width ? cursor - width + 1 : 0;
}

void TextInput::PlaceCursorFromMouse(int terminalColumn) {
  std::size_t promptWidth = 0;
  for (const auto &span : _prompt)
    promptWidth += Canvas::TextWidth(span.text);
  promptWidth = std::min(promptWidth,
                         static_cast<std::size_t>(std::max(0, Bounds().width)));
  const int requested = std::max(0, terminalColumn - Bounds().column -
                                        static_cast<int>(promptWidth));
  _cursor = ByteOffsetForCharacter(_value, VisibleStart());
  for (int index = 0; index < requested && _cursor < _value.size(); ++index) {
    _cursor = NextCharacter(_value, _cursor);
  }
}

Button::Button(std::string id, std::string label)
    : Widget(std::move(id)), _label(std::move(label)) {}

void Button::SetLabel(std::string label) { _label = std::move(label); }
void Button::SetPointer(std::optional<Point> point) {
  _hovered = point.has_value();
}

bool Button::Handle(const Event &event) {
  if (const auto *mouse = std::get_if<MouseEvent>(&event)) {
    if (mouse->action == MouseAction::Move)
      return false;
    if (mouse->action == MouseAction::Press &&
        mouse->button == MouseButton::Left) {
      _activated = true;
      return true;
    }
    return false;
  }
  const auto *key = std::get_if<KeyEvent>(&event);
  if (key && (key->key == Key::Enter ||
              (key->key == Key::Character && key->text == " "))) {
    _activated = true;
    return true;
  }
  return false;
}

void Button::Render(Canvas &canvas) const {
  const auto &bounds = Bounds();
  const bool active = Focused() || _hovered;
  const Style style{_hovered ? Color::Yellow
                             : (Focused() ? Color::Cyan : Color::Muted),
                    active};
  canvas.Text({bounds.column, bounds.row},
              Canvas::Clip("[ " + _label + " ]", bounds.width), style,
              bounds.width);
}

bool Button::TakeActivation() { return std::exchange(_activated, false); }

} // namespace termify::tui
