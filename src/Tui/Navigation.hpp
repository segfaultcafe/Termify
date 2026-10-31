#ifndef TERMIFY_TUI_NAVIGATION_HPP
#define TERMIFY_TUI_NAVIGATION_HPP

#include "Widget.hpp"

#include <chrono>

namespace termify::tui {

class TabBar : public Widget {
public:
  TabBar(std::string id, std::vector<std::string> labels);
  bool Focusable() const override { return true; }
  void Select(std::size_t index);
  std::size_t Selected() const { return _selected; }
  std::optional<std::size_t> Hovered() const { return _hovered; }
  void SetPointer(std::optional<Point> point) override;
  bool Handle(const Event &event) override;
  void Render(Canvas &canvas) const override;

private:
  std::optional<std::size_t> TabAt(Point point) const;
  std::vector<std::string> _labels;
  std::size_t _selected{0};
  std::optional<std::size_t> _hovered;
};

struct ListItem {
  std::string id, text;
};

class ListView : public Widget {
public:
  explicit ListView(std::string id);
  bool Focusable() const override { return true; }
  void SetItems(std::vector<ListItem> items);
  void SetEmptyText(std::string text);
  void SetDoubleClickActivates(bool enabled);
  bool Handle(const Event &event) override;
  void Render(Canvas &canvas) const override;
  std::optional<std::string> TakeActivation();
  std::optional<std::string> TakeDoubleClickActivation();
  std::optional<std::string> SelectedId() const;
  bool SelectId(const std::string &id);

private:
  std::size_t VisibleStart() const;
  std::vector<ListItem> _items;
  std::string _emptyText{"Nothing here yet."};
  std::size_t _selected{0};
  std::optional<std::string> _activation;
  std::optional<std::string> _doubleClickActivation;
  std::optional<std::string> _lastClickedId;
  std::optional<Point> _lastClickPoint;
  std::chrono::steady_clock::time_point _lastClickAt{};
  bool _doubleClickActivates{false};
};

} // namespace termify::tui
#endif
