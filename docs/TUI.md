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
`TextInput`, `Button`, `TabBar`, and `ListView` widgets. Player, History, and
Playlists share the frame, status, and command prompt. Playlist actions and the
Player's ASCII playback controls are ordinary focusable buttons, so the same
operation is available to mouse and keyboard users. The Player controls call
the same mixer operations as `resume`, `pause`, `next`, and `stop`; the shared
command prompt remains the source for queueing new songs.

`TabBar` owns selection, arrow-key navigation, and tab hit testing. `ListView`
owns a scrollable selection, keyboard/mouse navigation, and an activation event
consumed by the application. Stable item IDs preserve the selected play when
new rows arrive. Widgets receive display data, not database or mixer objects.

The Playlists view presents one list for playlists and another for ordered
tracks. Naming temporarily changes the shared command prompt; it does not add
text editing to list widgets. History's add button targets the playlist last
selected in the Playlists view. Application code translates action activations
into `LocalStore` operations and refreshes widgets from storage revisions.

The three-row file-style tab strip sits above the shared player frame. Its
active marker, bordered inactive tabs, hover color, and inline navigation hint
make click targets visible without relying on mouse support. `WidgetHost`
delivers pointer locations through `SetPointer` and clears them on leave or
keyboard input. Pointer movement never changes focus or activates a tab.

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
only while the interactive terminal session is active. All-motion mouse reports
enable hover; both motion tracking and SGR reporting are disabled on exit.

Alt+Left/Right switches views globally. Tab/Shift+Tab cycles visible focusable
widgets, and Escape focuses the command prompt. Left/right arrows on the tab
bar change views; up/down and paging keys in History move its selection. Enter
activates a history entry to queue it again. Enter or Space activates a focused
button, including the Player controls. Hidden widgets cannot receive focus or
input. The cursor is visible only when a focused widget supplies one.

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
history, playlists, and the disk-cache index. Playlist entries reference a
SHA-256 of the canonical media URL and keep metadata in a normalized library
table; they never own or delete cache files. The audio output thread enqueues a
history record after its first successful write; a separate writer performs
database I/O. The UI polls for revisions while persistent views are visible and
periodically refreshes to see changes from other instances.

The downloader consults the cache before fetching. Shared cache leases travel
with prepared audio through the queue and playback; advisory file locks prevent
another Termify instance from evicting active files. Cached files are not marked
temporary. The existing cleanup path still owns uncached temporary downloads.
Storage errors must not prevent ordinary playback, and history work must never
perform database I/O on the audio thread.
