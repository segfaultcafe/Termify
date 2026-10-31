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

- **v0.3 — Library foundation (released):** persistent history, bounded audio
  caching, navigable tabs, and the verified bundled-downloader fix. Further
  cancellation and error-recovery hardening remains part of the v1 gates.
- **v0.5 — Daily usability:** add seeking, shortcuts, persistent settings,
  playlists, and favorites.
- **v0.8 — Distribution:** ship AppImage builds, add ARM64, automate releases,
  add verified update checks and rollback, and test packages on clean supported
  distributions.
- **v0.9 — Stabilization:** freeze commands and configuration, finish the
  documentation, and focus on compatibility and bug fixes.
- **v1.0 — Supported release:** meet every release gate above and publish a
  documented compatibility and stability promise.

## Library foundation and next goal

Released in v0.3.0:

- Reusable tab and scrollable-list widgets with keyboard and mouse navigation.
- Player and History views; select a past play and queue it again.
- Embedded SQLite storage for the latest 1,000 plays, following XDG paths.
- A 256 MiB least-recently-used audio cache, with active tracks protected from
  eviction and cached audio reusable across restarts.
- The verified bundled-downloader fix retained from the discarded cassette
  branch. The decorative cassette feature is no longer planned.

Next, design a Playlists tab on the same widget and storage foundations:

- Create, rename, and delete local playlists.
- Add and remove songs, reorder entries, and play an entire playlist.
- Add songs from History and the current track without retyping the source.
- Define explicit history/cache management controls and safe schema migrations
  before adding more persistent library data.

## Not required for v1

Windows support, account login, cloud library synchronization, plugins, and a
graphical interface are intentionally outside the Linux 1.0 scope. Windows is
the next platform target once audio, terminal, process, and filesystem adapters
are ready for a native implementation.
