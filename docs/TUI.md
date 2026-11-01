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
`TextInput`, `Button`, `Knob`, `TabBar`, and `ListView` widgets. Player, History,
Playlists, and Search share the frame, status, and command prompt. Playlist
actions, Search actions, and the Player's ASCII playback controls are ordinary
focusable buttons, so the same operation is available to mouse and keyboard
users. The Player controls call
the same mixer operations as `resume`, `pause`, `next`, and `stop`; PLAY/PAUSE
is one state-aware button whose label and action follow the playback state. The
shared command prompt remains the source for queueing new songs. The labeled
buttons overlay the right side of the `NOW PLAYING` divider, preserving the
68 × 20 minimum layout and all three visible queue rows. The help row describes
where to find the controls without drawing a second, button-like legend.

An always-visible `QUIT` button is integrated into the player frame's title
bar. Activating it calls the mixer's normal shutdown path, stopping playback
and worker threads before the terminal is restored.

The Player summary embeds a state-aware track-repeat `Button` and a focusable
volume `Knob`. The knob rotates its marker as the value changes, consumes
mouse-wheel events in 5% steps, and offers arrow-key control when focused. Its
marker changes on every step so wheel input is immediately visible. Both
controls call the same mixer methods as the `repeat` and `volume` commands.
Volume changes are stored through `LocalStore` and restored when the mixer is
constructed.

The Player `ProgressBar` is pointer-aware without joining keyboard focus. A
click or drag emits an absolute fraction and a wheel event emits a five-second
relative adjustment. The application forwards both to the same mixer seek
boundary used by the `seek` command. Drag updates stay quiet in the status row;
wheel and CLI requests provide a textual confirmation. No keyboard seek
shortcut is assigned.

`TabBar` owns selection, arrow-key navigation, and tab hit testing. `ListView`
owns a scrollable selection, keyboard/mouse navigation, and an activation event
consumed by the application. Stable item IDs preserve the selected play when
new rows arrive. Widgets receive display data, not database or mixer objects.

History and Search share the same Play Now, Play Next, and Add to Playlist
action row. History hides the actions until a recorded play exists; Enter on a
history row retains Play Next semantics while double-click uses Play Now.

The Playlists view presents one list for playlists and another for ordered
tracks. Labeled command dividers distinguish playlist-level New, Rename, and
Delete, Play Selected, Shuffle, and Loop actions from track-level Add Current,
Remove, and Move actions. Naming temporarily changes the shared command
prompt; it does not add text editing to list widgets. History's add button
targets the playlist last selected in the Playlists view. Application code
translates action activations into `LocalStore` operations and refreshes
widgets from storage revisions.

A looping playlist is logically infinite but remains physically bounded. The
mixer replenishes a three-track tail as playlist entries are consumed, while
the summary displays `QUEUE ∞`. Manual Play Next requests retain priority; if
the bounded queue is full, its final playlist entry is deferred and restored
to the tail instead of rejecting the manual track or losing playlist order.
Loop-generated entries carry a distinct queue origin, allowing the live LOOP
toggle to remove future-cycle entries without touching the current cycle or
manual requests.

The three visible **UP NEXT** rows are one `ListView` keyed by stable queue
IDs. Selecting a row exposes Play Now, Remove, Move Up, Move Down, and Clear
buttons in the Player action row; double-click and Enter take the same Play Now
path. Clear changes to a Confirm button before mutating the queue. Reordering
is allowed within the manual or playlist tier but cannot move a playlist track
ahead of a manual request. Removing or moving playlist-origin entries changes
only the live session and never writes to `LocalStore`. The equivalent CLI
forms use the visible one-based positions: `queue play N`, `queue remove N`,
`queue move N up|down`, and `queue clear`.

The Search view owns a second `TextInput` dedicated to YouTube queries; the
shared command prompt remains visible and continues to accept fine-grained
commands. Results are canonical provider records, not user-entered metadata.
The three result buttons call the same queue and storage boundaries used by
the rest of the application and remain hidden while there are no selectable
results or a search is loading. The playlist action temporarily replaces
results with a compact picker; `✓` labels expose membership before activation
and remain visible after an add or duplicate no-op. The picker can create a
named playlist through the shared command prompt and immediately add its
pending track. The Player exposes the same picker for the current track from
an action embedded in the `UP NEXT` divider. Visible Search result labels are
numbered from one while their stable internal IDs preserve selection.

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

Search Enter submits the dedicated query when its input is focused and plays
the selected result immediately when the result list is focused. Explicit Play
Now, Play Next, and Add to Playlist buttons remain reachable by mouse or Tab.
Double-clicking a Search result takes the Play Now path. Double-clicking a
History entry also plays it immediately, while keyboard Enter keeps its Play
Next semantics.
The shared picker offers Add Here, New Playlist, and Cancel from Search or the
Player. Escape cancels naming first, then closes the picker, then returns to
the CLI.

## Rendering and state

The playback thread owns audio state. The visualization loop snapshots that
state, updates widget view data, renders a fresh canvas, and presents it. The
widget mutex protects interactive widget state such as focus and text input
from the input loop.

A seek request is stored atomically in output-frame units. The decoder wakes
even when the ring buffer is full, seeks and flushes FFmpeg state, clears stale
decoded frames, and increments a generation counter. The ALSA worker observes
that generation, drops already-buffered device audio, prepares the device, and
discards any chunk from the previous generation. This keeps seeking responsive
while playing or paused without racing the decoder and output threads.

Canvas clipping is mandatory: a widget must remain safe when its assigned area
is smaller than its preferred content. The current canvas treats each Unicode
code point as one terminal cell; full grapheme and wide-character measurement
is a future improvement.

## Persistent library boundary

`LocalStore` owns an embedded SQLite connection, versioned schema, bounded play
history, playlists, the disk-cache index, and persisted settings. Schema v3
stores the clamped output volume and verified provider titles with cached audio;
missing settings retain the normal default.
Playlist entries reference a
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

YouTube metadata search uses a separate yt-dlp `--flat-playlist` operation. Its
10-result responses live only in a 20-query in-memory LRU cache and expire
after 30 minutes. Search results may be persisted to playlists because their
canonical URL and metadata come from the provider; arbitrary CLI query text
still must be played, cached, and recorded in History before it is eligible.
Queue entries carry this verified title metadata through prefetch and playback.
When a raw URL or query is resolved, downloader metadata enriches the queued
entry; **UP NEXT** uses the title and falls back to the canonical URL only when
metadata remains unavailable. Local files use their filename stem.
