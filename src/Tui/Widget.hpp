#ifndef TERMIFY_TUI_WIDGET_HPP
#define TERMIFY_TUI_WIDGET_HPP

#include "Canvas.hpp"
#include "Event.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace termify::tui {

enum class Direction { Up, Down, Left, Right };

class Widget {
public:
  explicit Widget(std::string id);
  virtual ~Widget() = default;

  const std::string &Id() const;
  const Rect &Bounds() const;
  void SetBounds(Rect bounds);
  void SetVisible(bool visible);
  bool Visible() const;

  virtual void Render(Canvas &canvas) const = 0;
  virtual bool Handle(const Event &event);
  virtual bool Focusable() const;
  virtual void SetFocused(bool focused);
  // Local canvas coordinates; nullopt means the pointer left this widget.
  virtual void SetPointer(std::optional<Point> point);
  virtual std::optional<Point> Cursor() const;

protected:
  bool Focused() const;

private:
  std::string _id;
  Rect _bounds;
  bool _visible{true};
  bool _focused{false};
};

class WidgetHost {
public:
  template <typename WidgetType, typename... Args>
  WidgetType &Emplace(Args &&...args) {
    auto widget =
        std::make_unique<WidgetType>(std::forward<Args>(args)...);
    auto &reference = *widget;
    _widgets.push_back(std::move(widget));
    return reference;
  }

  void Render(Canvas &canvas) const;
  bool Dispatch(const Event &event);
  bool Focus(const std::string &id);
  std::optional<std::string> FocusedId() const;
  std::optional<Point> Cursor() const;

private:
  bool CycleFocus(int step);
  bool MoveFocus(Direction direction);
  Widget *HitTest(Point point) const;
  Widget *FocusedWidget() const;
  void SetFocused(Widget *widget);

  std::vector<std::unique_ptr<Widget>> _widgets;
  Widget *_focused{nullptr};
};

} // namespace termify::tui

#endif // TERMIFY_TUI_WIDGET_HPP
