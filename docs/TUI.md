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

The current screen is composed from `Frame`, `Rule`, `Label`, `ProgressBar`,
`TextInput`, `TabBar`, and `ListView` widgets. Player and History share the frame,
status, and command prompt. Playlists and future controls should build on the
same primitives or introduce another focused widget type.

`TabBar` owns selection, arrow-key navigation, and tab hit testing. `ListView`
owns a scrollable selection, keyboard/mouse navigation, and an activation event
consumed by the application. Stable item IDs preserve the selected play when
new rows arrive. Widgets receive display data, not database or mixer objects.

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

Alt+Left/Right switches views globally. Tab/Shift+Tab cycles visible focusable
widgets, and Escape focuses the command prompt. Left/right arrows on the tab
bar change views; up/down and paging keys in History move its selection. Enter
activates a history entry to queue it again. Hidden widgets cannot receive focus
or input. The cursor is visible only when a focused widget supplies one.

## Rendering and state

The playback thread owns audio state. The visualization loop snapshots that
state, updates widget view data, renders a fresh canvas, and presents it. The
widget mutex protects interactive widget state such as focus and text input
from the input loop.

Canvas clipping is mandatory: a widget must remain safe when its assigned area
is smaller than its preferred content. The current canvas treats each Unicode
code point as one terminal cell; full grapheme and wide-character measurement
is a future improvement.

## Persistent library boundary

`LocalStore` owns an embedded SQLite connection, versioned schema, bounded play
history, and the disk-cache index. The audio output thread enqueues a history
record after its first successful write; a separate writer performs database
I/O. The UI polls for revisions while History is visible and periodically
refreshes to see changes from other instances.

The downloader consults the cache before fetching. Shared cache leases travel
with prepared audio through the queue and playback; advisory file locks prevent
another Termify instance from evicting active files. Cached files are not marked
temporary. The existing cleanup path still owns uncached temporary downloads.
Storage errors must not prevent ordinary playback, and history work must never
perform database I/O on the audio thread.
