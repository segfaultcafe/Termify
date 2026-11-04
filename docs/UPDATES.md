# Update checks

`termify update --check` performs a read-only check for the newest stable
Termify release. It does not download an artifact, modify the current
installation, update yt-dlp, or write Termify configuration.

The command follows the HTTPS redirect from Termify's GitHub
`/releases/latest` endpoint with a 15-second network limit. Redirects are
restricted to HTTPS, and the final URL must exactly match the Termify release
tag format `vMAJOR.MINOR.PATCH`. Drafts and prereleases are intentionally not
reported by this command.

An available update is informational and returns exit status 0, just like an
up-to-date result. Network failures, timeouts, missing curl support, and invalid
release metadata return a nonzero status with an error on standard error.

The AppImage includes a private curl executable and its dependency closure.
Native archive and source installations use `curl` from `PATH`; install the
distribution's curl package if the command reports that it is unavailable.

## Verified downloads

`termify update --download` stages the correct stable-release artifact for the
running operating system, architecture, and installation type. It writes only
to the private Termify update cache (`$XDG_CACHE_HOME/termify/updates` on Linux
or `~/Library/Caches/termify/updates` on macOS). A completed command retains the
artifact and its `SHA256SUMS` manifest and prints the artifact path. It does not
extract, execute, or install the download.

The command requires `gh` from GitHub CLI in `PATH`. It verifies both the
manifest and artifact against attestations issued for
`segfaultcafe/Termify`, the tag ref, and Termify's tag-only release workflow.
Attestations from self-hosted runners are rejected. Termify then hashes the
artifact internally and requires an exact match with its single entry in the
verified manifest. Downloads use HTTPS-only redirects, size and time limits,
private randomly named partial files, and atomic staging.
Any failed download, provenance check, manifest parse, or checksum comparison
removes the partial files and returns nonzero.

AppImage and user-local/native-archive installations receive their matching
portable artifact. Known package-managed locations are refused so Termify does
not bypass `.deb`, `.rpm`, AUR, Homebrew, MacPorts, Flatpak, Snap, or Nix
ownership. Those installations must be updated with their package manager.

The full trust model is described in [RELEASE.md](RELEASE.md). Automatic
installation, atomic replacement, and rollback are separate milestones and are
not performed by either update command.
