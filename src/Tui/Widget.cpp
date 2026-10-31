#include "Widget.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace termify::tui {

Widget::Widget(std::string id) : _id(std::move(id)) {}
const std::string &Widget::Id() const { return _id; }
const Rect &Widget::Bounds() const { return _bounds; }
void Widget::SetBounds(Rect bounds) { _bounds = bounds; }
void Widget::SetVisible(bool visible) { _visible = visible; }
bool Widget::Visible() const { return _visible; }
bool Widget::Handle(const Event &) { return false; }
bool Widget::Focusable() const { return false; }
void Widget::SetFocused(bool focused) { _focused = focused; }
std::optional<Point> Widget::Cursor() const { return std::nullopt; }
bool Widget::Focused() const { return _focused; }

void WidgetHost::Render(Canvas &canvas) const {
  for (const auto &widget : _widgets) {
    if (widget->Visible()) {
      widget->Render(canvas);
    }
  }
}

bool WidgetHost::Dispatch(const Event &event) {
  if (const auto *mouse = std::get_if<MouseEvent>(&event)) {
    Widget *target = HitTest({mouse->column, mouse->row});
    if (!target)
      return false;
    if (mouse->action == MouseAction::Press && target->Focusable()) {
      SetFocused(target);
    }
    return target->Handle(event);
  }

  const auto *key = std::get_if<KeyEvent>(&event);
  if (!key)
    return false;
  if (key->key == Key::Tab)
    return CycleFocus(1);
  if (key->key == Key::BackTab)
    return CycleFocus(-1);

  if (_focused && _focused->Handle(event)) {
    return true;
  }

  switch (key->key) {
  case Key::ArrowUp:
    return MoveFocus(Direction::Up);
  case Key::ArrowDown:
    return MoveFocus(Direction::Down);
  case Key::ArrowLeft:
    return MoveFocus(Direction::Left);
  case Key::ArrowRight:
    return MoveFocus(Direction::Right);
  default:
    return false;
  }
}

bool WidgetHost::Focus(const std::string &id) {
  const auto match = std::find_if(
      _widgets.begin(), _widgets.end(),
      [&id](const auto &widget) { return widget->Id() == id; });
  if (match == _widgets.end() || !(*match)->Focusable() ||
      !(*match)->Visible()) {
    return false;
  }
  SetFocused(match->get());
  return true;
}

std::optional<std::string> WidgetHost::FocusedId() const {
  return _focused ? std::optional<std::string>(_focused->Id()) : std::nullopt;
}

std::optional<Point> WidgetHost::Cursor() const {
  return _focused ? _focused->Cursor() : std::nullopt;
}

bool WidgetHost::CycleFocus(int step) {
  if (_widgets.empty())
    return false;
  std::ptrdiff_t current = -1;
  if (_focused) {
    const auto match = std::find_if(
        _widgets.begin(), _widgets.end(),
        [this](const auto &widget) { return widget.get() == _focused; });
    current = std::distance(_widgets.begin(), match);
  } else if (step < 0) {
    current = 0;
  }
  for (std::size_t offset = 1; offset <= _widgets.size(); ++offset) {
    const auto count = static_cast<std::ptrdiff_t>(_widgets.size());
    const auto raw = current + step * static_cast<std::ptrdiff_t>(offset);
    const auto candidate = (raw % count + count) % count;
    auto *widget = _widgets[static_cast<std::size_t>(candidate)].get();
    if (widget->Visible() && widget->Focusable()) {
      SetFocused(widget);
      return true;
    }
  }
  return false;
}

bool WidgetHost::MoveFocus(Direction direction) {
  if (!_focused)
    return CycleFocus(1);
  const Point current = _focused->Bounds().Center();
  Widget *best = nullptr;
  long bestScore = std::numeric_limits<long>::max();
  for (const auto &candidate : _widgets) {
    if (candidate.get() == _focused || !candidate->Visible() ||
        !candidate->Focusable()) {
      continue;
    }
    const Point center = candidate->Bounds().Center();
    const int horizontal = center.column - current.column;
    const int vertical = center.row - current.row;
    int primary = 0;
    int secondary = 0;
    if (direction == Direction::Up && vertical < 0) {
      primary = -vertical;
      secondary = std::abs(horizontal);
    } else if (direction == Direction::Down && vertical > 0) {
      primary = vertical;
      secondary = std::abs(horizontal);
    } else if (direction == Direction::Left && horizontal < 0) {
      primary = -horizontal;
      secondary = std::abs(vertical);
    } else if (direction == Direction::Right && horizontal > 0) {
      primary = horizontal;
      secondary = std::abs(vertical);
    } else {
      continue;
    }
    const long score = static_cast<long>(primary) * 10000L + secondary;
    if (score < bestScore) {
      best = candidate.get();
      bestScore = score;
    }
  }
  if (!best)
    return false;
  SetFocused(best);
  return true;
}

Widget *WidgetHost::HitTest(Point point) const {
  for (auto iterator = _widgets.rbegin(); iterator != _widgets.rend();
       ++iterator) {
    if ((*iterator)->Visible() && (*iterator)->Bounds().Contains(point)) {
      return iterator->get();
    }
  }
  return nullptr;
}

Widget *WidgetHost::FocusedWidget() const { return _focused; }

void WidgetHost::SetFocused(Widget *widget) {
  if (_focused)
    _focused->SetFocused(false);
  _focused = widget;
  if (_focused)
    _focused->SetFocused(true);
}

} // namespace termify::tui
