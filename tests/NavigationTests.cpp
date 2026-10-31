#include "Tui/Navigation.hpp"
#include "Tui/Widgets.hpp"
#include <iostream>
#include <sstream>

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
  auto &button = host.Emplace<Button>("button", "PLAY ALL");
  tabs.SetBounds({0, 0, 70, 3});
  list.SetBounds({0, 4, 50, 3});
  input.SetBounds({0, 8, 50, 1});
  button.SetBounds({52, 8, 14, 1});
  CHECK(host.Focus("input"));
  host.Dispatch(MouseEvent{MouseButton::None, MouseAction::Move, 15, 1, {}});
  CHECK(tabs.Hovered() == 1 && tabs.Selected() == 0);
  CHECK(host.FocusedId() == "input"); // Hover never steals typing focus.
  Canvas hover(70, 9);
  host.Render(hover);
  std::ostringstream rendered;
  hover.Present(rendered, {1, 1});
  CHECK(rendered.str().find("› HISTORY") != std::string::npos);
  CHECK(rendered.str().find("Click a tab") != std::string::npos);
  host.Dispatch(MouseEvent{MouseButton::None, MouseAction::Move, 12, 1, {}});
  CHECK(!tabs.Hovered()); // Gap between tabs.
  host.Dispatch(MouseEvent{MouseButton::None, MouseAction::Move, 15, 1, {}});
  host.Dispatch(MouseEvent{MouseButton::None, MouseAction::Move, 80, 10, {}});
  CHECK(!tabs.Hovered()); // Pointer leaves the entire host.
  CHECK(!tabs.Handle(
      MouseEvent{MouseButton::Left, MouseAction::Press, 15, 3, {}}));
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
  CHECK(list.SelectId("3") && list.SelectedId() == "3");
  CHECK(!list.SelectId("missing") && list.SelectedId() == "3");
  CHECK(host.Dispatch(KeyEvent{Key::End, {}, {}}));
  CHECK(list.SelectedId() == "4");
  CHECK(host.Dispatch(
      MouseEvent{MouseButton::Left, MouseAction::Press, 1, 4, {}}));
  CHECK(list.SelectedId() == "2"); // Click the first visible scrolled row.
  CHECK(host.Dispatch(
      MouseEvent{MouseButton::WheelUp, MouseAction::Scroll, 1, 4, {}}));
  CHECK(list.SelectedId() == "1");
  list.SetVisible(false);
  CHECK(!host.Dispatch(KeyEvent{Key::Enter, {}, {}}));
  CHECK(!list.TakeActivation());
  CHECK(host.Focus("input"));
  CHECK(host.Dispatch(KeyEvent{Key::Character, "play", {}}));
  CHECK(input.Value() == "play");
  CHECK(host.Dispatch(
      MouseEvent{MouseButton::Left, MouseAction::Press, 54, 8, {}}));
  CHECK(button.TakeActivation());
  CHECK(!button.TakeActivation());
  CHECK(host.Focus("button"));
  CHECK(host.Dispatch(KeyEvent{Key::Enter, {}, {}}));
  CHECK(button.TakeActivation());
  list.SetItems({});
  CHECK(!list.SelectedId());
  TabBar future("three-tabs", {"PLAYER", "HISTORY", "PLAYLISTS"});
  future.SetBounds({0, 0, 70, 3});
  CHECK(future.Handle(KeyEvent{Key::ArrowLeft, {}, {}}));
  CHECK(future.Selected() == 2);
  CHECK(future.Handle(KeyEvent{Key::ArrowRight, {}, {}}));
  CHECK(future.Selected() == 0);
  CHECK(future.Handle(
      MouseEvent{MouseButton::Left, MouseAction::Press, 30, 1, {}}));
  CHECK(future.Selected() == 2);

  WidgetHost controls;
  auto &playPause = controls.Emplace<Button>("play-pause", "|> PLAY");
  auto &next = controls.Emplace<Button>("next", ">| NEXT");
  auto &stop = controls.Emplace<Button>("stop", "[] STOP");
  playPause.SetBounds({0, 0, 12, 1});
  next.SetBounds({13, 0, 12, 1});
  stop.SetBounds({26, 0, 12, 1});
  CHECK(controls.Focus("play-pause"));
  CHECK(controls.Dispatch(KeyEvent{Key::Enter, {}, {}}));
  CHECK(playPause.TakeActivation());
  CHECK(controls.Dispatch(KeyEvent{Key::Tab, {}, {}}));
  CHECK(controls.FocusedId() == "next");
  CHECK(controls.Dispatch(KeyEvent{Key::Character, " ", {}}));
  CHECK(next.TakeActivation());
  CHECK(controls.Dispatch(
      MouseEvent{MouseButton::Left, MouseAction::Press, 28, 0, {}}));
  CHECK(stop.TakeActivation());
  CHECK(controls.FocusedId() == "stop");
  Canvas controlsCanvas(38, 1);
  controls.Render(controlsCanvas);
  std::ostringstream controlsRendered;
  controlsCanvas.Present(controlsRendered, {1, 1});
  CHECK(controlsRendered.str().find("[ |> PLAY ]") != std::string::npos);
  CHECK(controlsRendered.str().find("[ >| NEXT ]") != std::string::npos);
  CHECK(controlsRendered.str().find("[ [] STOP ]") != std::string::npos);
  playPause.SetLabel("|| PAUSE");
  Canvas pausedControlsCanvas(38, 1);
  controls.Render(pausedControlsCanvas);
  std::ostringstream pausedControlsRendered;
  pausedControlsCanvas.Present(pausedControlsRendered, {1, 1});
  CHECK(pausedControlsRendered.str().find("[ || PAUSE ]") != std::string::npos);
  return 0;
}
