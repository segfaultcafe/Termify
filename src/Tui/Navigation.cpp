#include "Navigation.hpp"

#include <algorithm>
#include <utility>

namespace termify::tui {

TabBar::TabBar(std::string id, std::vector<std::string> labels)
    : Widget(std::move(id)), _labels(std::move(labels)) {}

void TabBar::Select(std::size_t index) {
  if (!_labels.empty())
    _selected = std::min(index, _labels.size() - 1);
}

std::optional<std::size_t> TabBar::TabAt(Point point) const {
  if (!Bounds().Contains(point) || point.row >= Bounds().row + 3)
    return std::nullopt;
  int column = Bounds().column;
  for (std::size_t index = 0; index < _labels.size(); ++index) {
    const int width = static_cast<int>(Canvas::TextWidth(_labels[index])) + 6;
    if (point.column >= column && point.column < column + width)
      return index;
    column += width + 1;
  }
  return std::nullopt;
}

void TabBar::SetPointer(std::optional<Point> point) {
  _hovered = point ? TabAt(*point) : std::nullopt;
}

bool TabBar::Handle(const Event &event) {
  if (_labels.empty())
    return false;
  if (const auto *key = std::get_if<KeyEvent>(&event)) {
    if (key->key == Key::ArrowRight) {
      Select((_selected + 1) % _labels.size());
      return true;
    }
    if (key->key == Key::ArrowLeft) {
      Select((_selected + _labels.size() - 1) % _labels.size());
      return true;
    }
    if (key->key == Key::Home) {
      Select(0);
      return true;
    }
    if (key->key == Key::End) {
      Select(_labels.size() - 1);
      return true;
    }
  }
  if (const auto *mouse = std::get_if<MouseEvent>(&event);
      mouse && mouse->action == MouseAction::Press &&
      mouse->button == MouseButton::Left) {
    if (const auto index = TabAt({mouse->column, mouse->row})) {
      Select(*index);
      return true;
    }
  }
  return false;
}

void TabBar::Render(Canvas &canvas) const {
  const auto bounds = Bounds();
  if (bounds.height <= 0 || bounds.width <= 0)
    return;
  int offset = 0;
  for (std::size_t index = 0; index < _labels.size(); ++index) {
    const bool selected = index == _selected;
    const bool hovered = _hovered == index;
    const int width = static_cast<int>(Canvas::TextWidth(_labels[index])) + 6;
    const Style border{hovered    ? Color::Yellow
                       : selected ? Color::Cyan
                                  : Color::Muted,
                       selected || hovered};
    const Style labelStyle{hovered    ? Color::Yellow
                           : selected ? Color::Cyan
                                      : Color::Default,
                           selected || hovered};
    std::string line;
    for (int cell = 0; cell < width - 2; ++cell)
      line += "─";
    const std::string label = "│ " +
                              std::string(selected  ? "● "
                                          : hovered ? "› "
                                                    : "  ") +
                              _labels[index] + " │";
    const std::string rows[] = {"╭" + line + "╮", label, "╰" + line + "╯"};
    for (int row = 0; row < std::min(3, bounds.height); ++row)
      canvas.Text({bounds.column + offset, bounds.row + row}, rows[row],
                  row == 1 ? labelStyle : border,
                  std::max(0, bounds.width - offset));
    offset += width + 1;
  }
  const std::string hint =
      Focused() ? "←/→ switch · Tab: content" : "Click a tab · Alt+←/→";
  if (bounds.height >= 2 &&
      bounds.width - offset >= static_cast<int>(Canvas::TextWidth(hint)) + 2)
    canvas.Text({bounds.column + offset + 2, bounds.row + 1}, hint,
                {Color::Muted});
}

ListView::ListView(std::string id) : Widget(std::move(id)) {}
void ListView::SetEmptyText(std::string text) { _emptyText = std::move(text); }

void ListView::SetItems(std::vector<ListItem> items) {
  const auto previous = SelectedId();
  _items = std::move(items);
  if (previous) {
    const auto found =
        std::find_if(_items.begin(), _items.end(),
                     [&](const auto &item) { return item.id == *previous; });
    if (found != _items.end())
      _selected = static_cast<std::size_t>(found - _items.begin());
  }
  _selected = _items.empty() ? 0 : std::min(_selected, _items.size() - 1);
}

std::size_t ListView::VisibleStart() const {
  const auto height = static_cast<std::size_t>(std::max(1, Bounds().height));
  return _selected >= height ? _selected - height + 1 : 0;
}

bool ListView::Handle(const Event &event) {
  if (_items.empty())
    return false;
  auto move = [&](int delta) {
    _selected = static_cast<std::size_t>(
        std::clamp(static_cast<int>(_selected) + delta, 0,
                   static_cast<int>(_items.size() - 1)));
  };
  if (const auto *key = std::get_if<KeyEvent>(&event)) {
    switch (key->key) {
    case Key::ArrowUp:
      move(-1);
      return true;
    case Key::ArrowDown:
      move(1);
      return true;
    case Key::PageUp:
      move(-std::max(1, Bounds().height));
      return true;
    case Key::PageDown:
      move(std::max(1, Bounds().height));
      return true;
    case Key::Home:
      _selected = 0;
      return true;
    case Key::End:
      _selected = _items.size() - 1;
      return true;
    case Key::Enter:
      _activation = _items[_selected].id;
      return true;
    default:
      break;
    }
  }
  if (const auto *mouse = std::get_if<MouseEvent>(&event)) {
    if (mouse->action == MouseAction::Scroll) {
      if (mouse->button == MouseButton::WheelUp) {
        move(-1);
        return true;
      }
      if (mouse->button == MouseButton::WheelDown) {
        move(1);
        return true;
      }
    }
    if (mouse->action == MouseAction::Press &&
        mouse->button == MouseButton::Left) {
      const auto row = mouse->row - Bounds().row;
      if (row >= 0 && row < Bounds().height) {
        const auto index = VisibleStart() + static_cast<std::size_t>(row);
        if (index < _items.size()) {
          _selected = index;
          return true;
        }
      }
    }
  }
  return false;
}

void ListView::Render(Canvas &canvas) const {
  const auto bounds = Bounds();
  if (_items.empty()) {
    canvas.Text({bounds.column, bounds.row},
                Canvas::Clip(_emptyText, std::max(0, bounds.width)),
                {Color::Muted}, bounds.width);
    return;
  }
  const auto start = VisibleStart();
  for (int row = 0; row < bounds.height && start + row < _items.size(); ++row) {
    const auto index = start + row;
    const bool selected = index == _selected;
    const Style style{selected ? Color::Cyan : Color::Default,
                      selected && Focused()};
    const auto text = std::string(selected ? "› " : "  ") + _items[index].text;
    canvas.Text({bounds.column, bounds.row + row},
                Canvas::Clip(text, std::max(0, bounds.width)), style,
                bounds.width);
  }
}

std::optional<std::string> ListView::TakeActivation() {
  return std::exchange(_activation, {});
}
std::optional<std::string> ListView::SelectedId() const {
  return _items.empty() ? std::nullopt
                        : std::optional<std::string>(_items[_selected].id);
}

bool ListView::SelectId(const std::string &id) {
  const auto found =
      std::find_if(_items.begin(), _items.end(),
                   [&id](const auto &item) { return item.id == id; });
  if (found == _items.end())
    return false;
  _selected = static_cast<std::size_t>(found - _items.begin());
  return true;
}

} // namespace termify::tui
