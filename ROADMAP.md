# Termify Roadmap

## Version 1 goal

Termify 1.0 will be a polished Linux and macOS terminal music player that
installs with one download, plays local and online audio reliably, and never
leaves the terminal or system in a broken state.

## Version 1 release gates

### Portable distribution

- Provide self-contained x86_64 and ARM64 AppImages.
- Provide tested macOS builds for Apple Silicon and Intel Macs with the
  downloader bundled and verified.
- Require no Python or separately installed yt-dlp.
- Support current Ubuntu LTS, Debian stable, Fedora, Arch, and openSUSE.
- Support maintained macOS releases on Apple Silicon and Intel while the
  required toolchain and media dependencies remain available.
- Publish verified checksums and bundled-dependency license information.
- Add native `.deb`, `.rpm`, and AUR packages where they improve integration.

### Safe updates

- Provide `termify update --check` to report available stable releases without
  making changes. Implemented for the v0.8 milestone.
- Provide an explicit `termify update` flow for user-local and AppImage
  installations; never update silently in the background. Implemented for the
  v0.8 milestone.
- Verify downloaded artifacts with published checksums and release signatures
  before installation. Implemented as a non-installing verified staging step
  for the v0.8 milestone.
- Replace executables atomically, retain the previous working version, and
  support a simple rollback after a failed update. Implemented for the v0.8
  milestone for AppImage and user-local archive installations.
- Update the bundled yt-dlp independently when extractor fixes are needed,
  without requiring a full Termify release.
- Detect package-managed installations and direct users to their package
  manager instead of overwriting managed files.
- Back up and migrate configuration safely when its schema changes.

### Daily usability

- Keep queue prefetching bounded and avoid duplicate downloads.
- Provide mouse and command-line seeking without adding new keyboard
  shortcuts.
- Persist volume, configuration, history, and the upcoming queue.
- Support playlists and a permanent Liked Songs collection.
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
- **v0.4 — Player controls and search (released):** clickable ASCII playback
  controls, plus a keyboard- and mouse-navigable YouTube search-results tab
  with Play Now, Play Next, and playlist-picker actions. Ten-result searches
  use a bounded 30-minute session cache while retaining CLI queueing.
- **v0.5 — Daily usability (released):** mouse and command-line
  seeking, persistent volume, metadata-rich queue labels, and continuously
  replenished looping-playlist queues. Favorites and playlist import/export
  remain future work.
- **v0.6 — Queue management (released):** select upcoming tracks and
  play them immediately from the Player, with precise CLI removal and
  reordering that preserves manual-request priority and leaves saved playlists
  untouched.
- **v0.7.0 — macOS port and liked songs (released):** persistent Liked Songs
  and a Player heart toggle; a platform audio boundary and native Core Audio
  backend; macOS terminal, process, filesystem, and executable discovery;
  verified native packages for Apple Silicon and Intel; and preserved Linux
  behavior.
- **v0.8 — Distribution (in progress):** publish native packages from validated
  version tags with generated notes, complete package smoke checks, SHA-256
  manifests, keyless build provenance, and self-contained Linux x86_64 and
  ARM64 AppImages, a read-only stable-release update check, and attested,
  checksum-verified artifact staging, atomic replacement, and rollback. Native
  `.deb` packages are built, installed, started, update-guarded, and removed on
  clean Debian 12 and Ubuntu 24.04 runners for amd64 and arm64. RPM, AUR, and
  broader distribution packages remain v1 portability work where they improve
  integration.
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

The playlist MVP for v0.3.1 is implemented on the same widget and storage
foundations:

- Create, rename, and safely delete local playlists.
- Add cached, previously heard songs by canonical URL hash; remove and reorder
  entries; play an entire bounded playlist.
- Add songs from History and the current track without retyping the source.
- Shuffle playback and loop the whole playlist independently of track repeat.
- Keep playlist order active across `next` and prioritize manually queued
  tracks before the remaining playlist without discarding the session.
- Operate every playlist action with either mouse or keyboard.

Still next:

- Add broader playlist import/export workflows.
- Expand persistence beyond volume to configuration and the upcoming queue.
- Define explicit history/cache management controls and safe schema migrations
  before adding more persistent library data. Schema v3 adds settings-backed
  volume persistence; user-facing management controls remain.

## Not required for v1

Windows support, account login, cloud library synchronization, plugins, and a
graphical interface are intentionally outside the 1.0 scope. Windows is the
next platform target after the Linux and macOS adapters are established and
the supported 1.0 release is complete.
