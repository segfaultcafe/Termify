#ifndef TERMIFY_TUI_NAVIGATION_HPP
#define TERMIFY_TUI_NAVIGATION_HPP

#include "Widget.hpp"

namespace termify::tui {

class TabBar : public Widget {
public:
  TabBar(std::string id, std::vector<std::string> labels);
  bool Focusable() const override { return true; }
  void Select(std::size_t index);
  std::size_t Selected() const { return _selected; }
  bool Handle(const Event &event) override;
  void Render(Canvas &canvas) const override;

private:
  std::vector<std::string> _labels;
  std::size_t _selected{0};
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
  bool Handle(const Event &event) override;
  void Render(Canvas &canvas) const override;
  std::optional<std::string> TakeActivation();
  std::optional<std::string> SelectedId() const;

private:
  std::size_t VisibleStart() const;
  std::vector<ListItem> _items;
  std::string _emptyText{"Nothing here yet."};
  std::size_t _selected{0};
  std::optional<std::string> _activation;
};

} // namespace termify::tui
#endif
