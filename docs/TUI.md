# TUI architecture

Termify renders its interface through reusable widgets. Application code should
not write ANSI positioning, color, border, or mouse-mode sequences directly.

## Layers

1. `Terminal` owns raw mode, the alternate screen, mouse reporting, and safe
   restoration on exit.
2. `InputDecoder` converts terminal bytes into typed keyboard and mouse events.
3. `Canvas` provides clipped, styled, coordinate-based drawing.
4. `Widget` gives each visible component bounds, rendering, focus, keyboard
   handling, mouse hit testing, and an optional cursor.
5. `WidgetHost` owns widgets, renders them in order, routes clicks, cycles focus,
   and moves focus spatially with arrow keys.

The current screen is composed from `Frame`, `Rule`, `Label`, `ProgressBar`, and
`TextInput` widgets. New tabs, history rows, playlists, controls, and animations
should build on the same primitives or introduce another focused widget type.

## Adding a widget

Derive from `Widget`, update only the cells inside `Bounds()`, and return `true`
from `Handle()` only when the event was consumed.

```cpp
class Button : public Widget {
public:
  using Widget::Widget;

  bool Focusable() const override { return true; }
  void Render(Canvas &canvas) const override;
  bool Handle(const Event &event) override;
};
```

Register the instance once with `WidgetHost::Emplace`, retain the returned
reference, and update its bounds and view data before rendering. Widget order is
paint order; mouse hit testing uses the reverse order so the topmost visible
widget receives a click.

## Coordinates and input

Canvas and widget coordinates are zero-based and local to the Termify view.
Terminal mouse reports are one-based and screen-relative; `Application`
translates them to view coordinates before dispatch.

Keyboard events preserve modifiers and distinguish navigation, editing,
activation, tab traversal, interruption, and text. Mouse events distinguish
press, release, movement, and wheel scrolling. SGR mouse reporting is enabled
only while the interactive terminal session is active.

## Rendering and state

The playback thread owns audio state. The visualization loop snapshots that
state, updates widget view data, renders a fresh canvas, and presents it. The
widget mutex protects interactive widget state such as focus and text input
from the input loop.

Canvas clipping is mandatory: a widget must remain safe when its assigned area
is smaller than its preferred content. The current canvas treats each Unicode
code point as one terminal cell; full grapheme and wide-character measurement
is a future improvement.
