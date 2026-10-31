<p align="center">
  <img src="assets/termify-playlists.png" alt="Termify playing a track with Player, History, and Playlists tabs" width="100%">
</p>

<h1 align="center">Termify</h1>

<p align="center">
  A focused terminal audio player for Linux, built with modern C++, FFmpeg,
  ALSA, and an included standalone yt-dlp.
</p>

## Highlights

- Search for music, paste a supported URL, or play a local audio file
- Responsive, centered terminal interface with a live waveform
- Title, artist, album, elapsed time, duration, and playback progress
- Bounded 20-track queue with one-at-a-time background prefetching
- Player, History, and Playlists tabs with keyboard and mouse controls
- Clickable ASCII playback controls alongside the command prompt
- Persistent playlists built from previously heard, cached online tracks
- Local play history and a bounded audio cache for replaying downloaded tracks
- Pause, resume, skip, repeat, stop, and software volume control
- Editable Unicode-aware input with cursor movement and deletion
- Clean cancellation, temporary-file cleanup, and readable error messages
- Self-contained online fetching: no Python or separate yt-dlp installation

In development for **v0.4.0**: clickable Player controls are available first;
a navigable YouTube Search tab is the next part of the release.

**v0.3.1** fixes playlist advancement with `next`, preserves playlist loops,
and prioritizes manually queued tracks without ending the playlist session.

The latest packaged release is **v0.3.0**, which introduced persistent play
history, a reusable audio cache, and keyboard- and mouse-friendly tabs.

## Install the Linux release

Download `termify-v0.3.0-linux-x86_64.tar.gz` and its checksum from the
[v0.3.0 GitHub release](https://github.com/segfaultcafe/Termify/releases/tag/v0.3.0).

```bash
sha256sum -c termify-v0.3.0-linux-x86_64.tar.gz.sha256
tar -xzf termify-v0.3.0-linux-x86_64.tar.gz
cd termify-v0.3.0-linux-x86_64
./install.sh
```

The installer places `termify` in `~/.local/bin` by default. You can also pass
an installation prefix explicitly. For a system-wide installation:

```bash
sudo ./install.sh /usr/local
```

The archive includes the official standalone `yt-dlp` 2026.10.26 executable.
The installer normally keeps it private in `~/.local/lib/termify`, so it does
not replace or interfere with a system installation. If that directory is not
writable, it safely falls back to placing yt-dlp beside the Termify executable.
Python and a separate yt-dlp install are not required.

### Distribution setup

Termify uses FFmpeg libraries for decoding, ALSA for audio output, and SQLite
for local storage. Install those runtime dependencies before running the
release. The archive is not yet a universal Linux binary; check the ABI
compatibility note below before installing.

**Arch Linux / Manjaro**

```bash
sudo pacman -S ffmpeg alsa-lib sqlite
```

**Fedora**

```bash
sudo dnf install ffmpeg-free alsa-lib sqlite-libs
```

**Debian / Ubuntu / Linux Mint**

```bash
sudo apt update
sudo apt install ffmpeg libasound2 libsqlite3-0
```

On Debian 13 and newer, `libasound2` may be provided by `libasound2t64` and is
normally selected automatically.

**openSUSE**

```bash
sudo zypper install ffmpeg libasound2 libsqlite3-0
```

Then use the same checksum, extraction, and `./install.sh` commands shown
above. The v0.3.0 prebuilt binary is produced on x86_64 Arch Linux and expects
FFmpeg 9 ABI libraries (`libavformat.so.63`, `libavcodec.so.63`,
`libavutil.so.61`, and `libswresample.so.7`), plus `libsqlite3.so.0` and
`libasound.so.2`. It is tested on the build host, not certified across all
distributions. On distributions with an older FFmpeg ABI, use the source-build
instructions below; the build
will link against that distribution's native FFmpeg version.

Alpine uses musl rather than glibc, and NixOS does not expose system libraries
through the standard filesystem layout. For those systems, build from source
inside the native package environment instead of using this glibc archive.

## Bundled online playback

Online search and URL playback work immediately with the yt-dlp included in the
release. Termify checks its private bundled copy automatically. To test it from
an extracted archive:

```bash
./yt-dlp --version
```

Advanced users can override the bundled executable without modifying Termify:

```bash
TERMIFY_YTDLP=/absolute/path/to/another/yt-dlp termify
```

## Quick start

Start the interface:

```bash
termify
```

Then enter commands at the `termify ›` prompt:

| Command | Description |
| --- | --- |
| `play <name, URL, or file>` | Play if idle, otherwise queue the track next |
| `pause` / `resume` | Pause or resume the current track |
| `next` | Skip to the next queued track |
| `repeat` | Toggle repeating the current track |
| `volume 60` | Set volume from 0 to 100 percent |
| `volume +10` / `volume -10` | Adjust volume relative to its current level |
| `queue` | Show the number of queued tracks |
| `history` / `player` | Switch to the History or Player tab |
| `playlists` | Open the playlist manager |
| `clear` | Clear upcoming tracks |
| `stop` | Stop playback and clear the queue |
| `help` | Show the interactive command list |
| `help <command>` | Show focused usage, such as `help volume` |
| `exit` | Leave Termify cleanly |

Examples:

```text
play daft punk veridis quo
play https://www.youtube.com/watch?v=...
play /home/me/Music/song.flac
volume 65
next
```

### Navigate the interface

- The Player tab has ASCII controls for **PLAY** (`|>`), **PAUSE** (`||`),
  **NEXT** (`->|`), and **STOP** (`[]`). Click one, or reach it with Tab and
  activate it with Enter or Space. PLAY resumes paused audio; the command
  prompt remains available for adding new songs.
- The bordered file-style tabs sit above the player. A cyan dot marks the
  active view; hovering highlights a tab without taking focus from your typing.
- Use **Alt+Left / Alt+Right** or click a tab to switch views.
- **Tab / Shift+Tab** cycles focus. Left/right arrows switch tabs when the tab
  bar is focused; **Esc** returns to the command prompt.
- In History, use **Up / Down**, **Page Up / Page Down**, or **Home / End** to
  select a play. The mouse wheel scrolls and a click selects a row.
- Press **Enter** on a selected history row to queue that song again. The
  `history` command opens the tab and focuses the list directly.
- In History, choose **ADD TO PLAYLIST** (or press **A** while the list is
  focused) to add the selected cached play to the playlist last selected in
  the Playlists tab.
- The Playlists tab has separate playlist and track lists. Click a row or use
  the arrow keys, then use Tab/Shift+Tab to reach **NEW**, **RENAME**,
  **DELETE**, **ADD CURRENT**, **REMOVE**, **MOVE UP/DOWN**, **PLAY ALL**,
  **SHUFFLE**, and **LOOP**. Enter or Space activates a focused button.
- New and renamed playlist names are entered at the bottom prompt; Escape
  cancels the naming prompt. Deletion requires activating **DELETE** twice.
- The interactive layout requires a terminal of at least **68 × 21** cells.

### Local history and cache

Termify keeps the latest **1,000 plays** in an embedded SQLite database. A play
is recorded when audio first reaches the output device, not when a download
starts. Repeated plays are separate entries, newest first. History survives
restarts; queued tracks and playback position do not yet persist.

Online tracks are prefetched and kept in a **256 MiB** disk cache. Reusing the
same search text or URL—including replaying a history entry—avoids another
download while that audio remains cached. `[cache]` marks available cached
audio in History. Different searches for the same song are separate cache
keys. Least-recently-used files are evicted first, but queued or playing files
are protected. Oversized tracks or a cache full of protected files use temporary
downloads, removed after use. Local files are played in place, never copied or
deleted by the cache.

Default storage locations:

| Data | Location |
| --- | --- |
| History and cache index | `~/.local/share/termify/library.sqlite3` |
| Downloaded audio | `~/.cache/termify/audio/` |

Absolute `XDG_DATA_HOME` and `XDG_CACHE_HOME` values override these base
directories. SQLite runs inside Termify: no database server, SQLite command-line
tool, Python, or account is required. If storage cannot be opened, playback
continues without persistence and the interface reports the problem.

### Playlists

Playlists contain up to **20** tracks so playing one remains within Termify's
bounded queue. A track can be added only after it has reached audio output,
appears in History, and still has a valid cache entry. Termify stores a SHA-256
identity for yt-dlp's canonical media URL plus its embedded title, artist,
album, and duration. It never treats a newly typed search phrase as playlist
truth and never copies audio into a playlist.

**SHUFFLE** randomizes the order each time the playlist starts and for every
new loop. **LOOP** repeats the whole playlist independently of the `repeat`
command for one track. Starting a playlist disables single-track repeat.
Tracks added with `play` while a playlist is active are inserted ahead of the
remaining playlist tracks without ending the playlist session. Multiple manual
additions retain their enqueue order.
Removing a track or deleting a playlist does not delete cached audio. If a
cache entry is later evicted, the saved canonical URL lets Termify fetch it
again when the playlist is played.

## Build from source

Requirements:

- Linux with PipeWire or an ALSA-compatible playback device
- CMake 3.16 or newer
- A C++23-capable compiler
- `pkg-config`
- FFmpeg development libraries: `libavformat`, `libavcodec`, `libavutil`, and
  `libswresample`
- ALSA development libraries
- SQLite3 development libraries (no database service is needed)
- `curl` and `sha256sum` to fetch and verify the bundled downloader

```bash
git clone https://github.com/segfaultcafe/Termify.git
cd Termify
make build
make run
```

`make build` compiles the Debug target and runs the tests. Other useful targets:

```bash
make test       # rebuild and run the test suite
make package    # create the tested Linux release archive and checksum
make install    # build, test, and install to /usr/local/bin
```

`make build` and `make package` use the same pinned official standalone yt-dlp
release and verify its SHA-256 digest. The first build downloads it into the
project cache; subsequent builds reuse the verified copy without network access.
Python and a separate yt-dlp installation are not required. The build reports
the bundled downloader version so an outdated copy is easy to spot.

The terminal interface is composed from reusable, keyboard- and mouse-aware
widgets. See [docs/TUI.md](docs/TUI.md) before adding or changing interface
components.

## Platform status

Termify currently supports Linux. Windows support is planned around a
cross-platform audio backend and platform-specific terminal and process
adapters; the decoder, queue, metadata, progress, and playback state are
already portable.

The supported-release goals and milestones are tracked in
[ROADMAP.md](ROADMAP.md).

## License

[MIT](LICENSE)
