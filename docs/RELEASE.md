# Release checklist

Use this checklist for every tagged Termify release. A green pull-request build
is necessary but does not replace the real-device playback checks below.

## Prepare

- Set the intended version in `CMakeLists.txt` and update user-facing version
  references, the roadmap, and the bug-report template.
- Confirm the pinned yt-dlp assets and SHA-256 digests against the official
  yt-dlp GitHub release.
- Run `make test`, `git diff --check`, and shell syntax checks locally.
- Open a release-preparation pull request and require the complete CI matrix.

CI must build, test, start, package, checksum, install, and run `--version` for:

- Linux `x86_64`
- macOS Intel `x86_64`
- macOS Apple Silicon `arm64`

## Manual release candidate checks

On a physical Apple Silicon Mac and, when available, a physical Intel Mac:

1. Install the matching CI package into a clean temporary prefix.
2. Confirm local-file and online playback through the native audio device.
3. Exercise play/pause, next, stop, seeking, volume, track repeat, playlist
   shuffle/loop, and clean exit.
4. Confirm `♡ LIKE` is visible immediately left of **ADD TO PLAYLIST**, changes
   to `♥ LIKED`, persists after restart, and toggles the Liked Songs entry.
5. Confirm the terminal is restored after normal exit and Ctrl-C.

Repeat a basic playback and clean-exit check with the Linux release candidate.
Record the tested OS versions, architectures, and package artifact names in the
GitHub release notes.

## Publish

- Merge only after CI and manual release-candidate checks pass.
- Tag the merge commit as `vX.Y.Z`.
- Attach each native `.tar.gz` archive and `.sha256` file to the GitHub release.
- Verify every uploaded checksum and each staged executable's `--version`.
- Publish concise release notes covering features, supported platforms,
  dependencies, migrations, and known limitations.
- Keep the macOS signing/notarization limitation visible until signed release
  artifacts are part of the publishing workflow.
