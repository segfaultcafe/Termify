#include "Tui/Navigation.hpp"
#include "Tui/Widgets.hpp"
#include <iostream>

using namespace termify::tui;
#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "Failed at " << __LINE__ << ": " #condition << '\n';        \
      return 1;                                                                \
    }                                                                          \
  } while (false)

int main() {
  WidgetHost host;
  auto &tabs = host.Emplace<TabBar>(
      "tabs", std::vector<std::string>{"PLAYER", "HISTORY"});
  auto &list = host.Emplace<ListView>("list");
  auto &input = host.Emplace<TextInput>("input");
  tabs.SetBounds({0, 0, 50, 1});
  list.SetBounds({0, 2, 50, 3});
  input.SetBounds({0, 6, 50, 1});
  CHECK(host.Focus("tabs"));
  CHECK(host.Dispatch(KeyEvent{Key::ArrowRight, {}, {}}));
  CHECK(tabs.Selected() == 1);
  CHECK(host.Dispatch(KeyEvent{Key::ArrowLeft, {}, {}}));
  CHECK(tabs.Selected() == 0);
  CHECK(host.Dispatch(
      MouseEvent{MouseButton::Left, MouseAction::Press, 15, 0, {}}));
  CHECK(tabs.Selected() == 1);
  list.SetItems(
      {{"1", "First"}, {"2", "Second"}, {"3", "Third"}, {"4", "Fourth"}});
  CHECK(host.Dispatch(KeyEvent{Key::Tab, {}, {}}));
  CHECK(host.FocusedId() == "list");
  CHECK(host.Dispatch(KeyEvent{Key::ArrowDown, {}, {}}));
  CHECK(list.SelectedId() == "2");
  CHECK(host.Dispatch(KeyEvent{Key::Enter, {}, {}}));
  CHECK(list.TakeActivation() == "2");
  CHECK(!list.TakeActivation());
  list.SetItems({{"5", "New play"},
                 {"1", "First"},
                 {"2", "Second"},
                 {"3", "Third"},
                 {"4", "Fourth"}});
  CHECK(list.SelectedId() ==
        "2"); // New plays must not move the selected track.
  CHECK(host.Dispatch(KeyEvent{Key::End, {}, {}}));
  CHECK(list.SelectedId() == "4");
  CHECK(host.Dispatch(
      MouseEvent{MouseButton::Left, MouseAction::Press, 1, 2, {}}));
  CHECK(list.SelectedId() == "2"); // Click the first visible scrolled row.
  CHECK(host.Dispatch(
      MouseEvent{MouseButton::WheelUp, MouseAction::Scroll, 1, 2, {}}));
  CHECK(list.SelectedId() == "1");
  list.SetVisible(false);
  CHECK(!host.Dispatch(KeyEvent{Key::Enter, {}, {}}));
  CHECK(!list.TakeActivation());
  CHECK(host.Focus("input"));
  CHECK(host.Dispatch(KeyEvent{Key::Character, "play", {}}));
  CHECK(input.Value() == "play");
  list.SetItems({});
  CHECK(!list.SelectedId());
  return 0;
}
