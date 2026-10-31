# Termify Roadmap

## Version 1 goal

Termify 1.0 will be a polished Linux terminal music player that installs with
one download, plays local and online audio reliably, and never leaves the
terminal or system in a broken state.

## Version 1 release gates

### Portable distribution

- Provide a self-contained x86_64 AppImage, with ARM64 as the next target.
- Require no Python or separately installed yt-dlp.
- Support current Ubuntu LTS, Debian stable, Fedora, Arch, and openSUSE.
- Publish verified checksums and bundled-dependency license information.
- Add native `.deb`, `.rpm`, and AUR packages where they improve integration.

### Safe updates

- Provide `termify update --check` to report available releases without making
  changes.
- Provide an explicit `termify update` flow for user-local and AppImage
  installations; never update silently in the background.
- Verify downloaded artifacts with published checksums and release signatures
  before installation.
- Replace executables atomically, retain the previous working version, and
  support a simple rollback after a failed update.
- Update the bundled yt-dlp independently when extractor fixes are needed,
  without requiring a full Termify release.
- Detect package-managed installations and direct users to their package
  manager instead of overwriting managed files.
- Back up and migrate configuration safely when its schema changes.

### Daily usability

- Keep queue prefetching bounded and avoid duplicate downloads.
- Add seeking and documented keyboard shortcuts.
- Persist volume, configuration, history, and the upcoming queue.
- Support playlists and favorites.
- Keep the interface usable after terminal resizing and in small terminals.
- Provide complete command help, a man page, and useful diagnostics.

### Reliability

- Recover cleanly from network, extractor, audio-device, and disk failures.
- Leave no orphaned downloader processes or abandoned temporary files.
- Securely create cache files and follow the XDG directory conventions.
- Test EOF, Ctrl-C, Unicode input, terminal resizing, failed downloads, rapid
  command sequences, and long playback sessions.
- Run sanitizer builds and integration tests with fake downloader and audio
  backends in continuous integration.

### Maintainable architecture

- Put online sources behind a provider interface so yt-dlp is replaceable.
- Separate playback state from terminal rendering.
- Isolate terminal, process, filesystem, and audio platform code behind clear
  adapters.
- Preserve local playback when online providers are unavailable.

## Milestones

- **v0.3 — Reliability:** harden the queue, downloader cancellation, cleanup,
  error recovery, and terminal-resize behavior.
- **v0.5 — Daily usability:** add seeking, shortcuts, persistent settings,
  history, playlists, and favorites.
- **v0.8 — Distribution:** ship AppImage builds, add ARM64, automate releases,
  add verified update checks and rollback, and test packages on clean supported
  distributions.
- **v0.9 — Stabilization:** freeze commands and configuration, finish the
  documentation, and focus on compatibility and bug fixes.
- **v1.0 — Supported release:** meet every release gate above and publish a
  documented compatibility and stability promise.

## Next goal after the TUI foundation

Add a dedicated animated cassette widget between **UP NEXT** and
**LIVE WAVEFORM**.

- Draw two reels connected by a tape path using the shared widget and canvas
  APIs.
- Derive reel fill from normalized playback progress: the left reel gradually
  shrinks while the right reel grows from the beginning to the end of a track.
- Advance a subtle rotation phase from elapsed playback time so movement is
  slow and continuous, freezes while paused, and catches up correctly after a
  seek.
- Keep the animation decorative and independent from the decoder and mixer.
- Provide a compact representation or hide decorative detail when the terminal
  is too narrow or short.
- Test deterministic frames at 0%, 50%, and 100% progress, plus pause, resume,
  seek, and unknown-duration states.

The cassette layout and visual language will be designed after the widget
foundation is merged, in a separate feature branch.

## Not required for v1

Windows support, account login, cloud library synchronization, plugins, and a
graphical interface are intentionally outside the Linux 1.0 scope. Windows is
the next platform target once audio, terminal, process, and filesystem adapters
are ready for a native implementation.
