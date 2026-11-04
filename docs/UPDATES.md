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

The full trust model is described in [RELEASE.md](RELEASE.md).

## Atomic installation and rollback

`termify update` performs the same release check, provenance verification, and
checksum verification as `termify update --download`, then installs the staged
artifact. It supports AppImage and user-local archive installations. Known
package-managed locations are refused before any installation changes.

AppImages are copied to a private file beside the installed AppImage, synced,
made executable, and renamed over the installed file. A rename on the same
filesystem makes the visible replacement atomic. User-local archives are
extracted privately and their bundled yt-dlp and Termify executable are each
replaced through the same platform filesystem operation.

Before replacement, Termify copies the installed files to its private update
cache and atomically publishes a rollback record scoped to the executable path.
Publishing that record first preserves recovery if Termify, the terminal, or
the machine stops between the two user-local file replacements. An ordinary
replacement failure restores every file changed by the transaction.
An interrupted transaction remains marked as pending; another install is
refused until `termify update --rollback` restores the pre-update files.

`termify update --rollback` restores the files recorded by the last successful
update, then deletes that rollback record. A second successful update replaces
the rollback point, so rollback always returns to the immediately preceding
working installation. Separate executable paths use separate records.

The updater does not modify configuration or databases. A per-installation
exclusive lock rejects concurrent update and rollback processes for the same
executable.

## Independent yt-dlp updates

`termify update yt-dlp --check` compares the active downloader with Termify's
independent yt-dlp release channel. `termify update yt-dlp` downloads and
installs an available update, while `termify update yt-dlp --rollback` restores
the previous private update or removes it to return to the downloader bundled
with Termify.

Maintainers publish these updates as GitHub prereleases tagged
`yt-dlp-vYYYY.MM.DD[.REVISION]`. They are intentionally separate from stable
`vMAJOR.MINOR.PATCH` Termify releases, so they do not change what
`termify update --check` reports. The publishing workflow downloads the three
official standalone executables, verifies yt-dlp's signed `SHA2-256SUMS`
against its pinned signing-key fingerprint, and republishes an attested
SHA-256 manifest for Linux x86_64, Linux ARM64, and universal macOS.

The client accepts only assets attested for Termify's dedicated
`.github/workflows/yt-dlp-release.yml` workflow on `master`, rejects
self-hosted-runner provenance, and independently checks the selected asset's
SHA-256 digest and reported version. This verification requires GitHub CLI
(`gh`) in `PATH`. Installation uses a private data path and atomic replacement
with a separate lock and rollback record. AppImages can therefore update their
extractor despite their immutable filesystem; their embedded downloader
remains untouched as a fallback.

Known package-managed installations refuse independent downloader updates.
They never load a private update left by an AppImage or archive installation;
the distribution package continues to own and update its bundled yt-dlp.
