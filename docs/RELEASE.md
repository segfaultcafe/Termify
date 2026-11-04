# Release checklist

Termify releases are built and published by `.github/workflows/release.yml`
from stable `vMAJOR.MINOR.PATCH` tags. A green pull-request build is necessary
but does not replace the real-device playback checks below.

## Prepare

- Set the intended version in `CMakeLists.txt` and update user-facing version
  references, the roadmap, and the bug-report template.
- Confirm the pinned yt-dlp assets and SHA-256 digests against the official
  yt-dlp GitHub release.
- Run `make test`, `git diff --check`, and shell syntax checks locally.
- Open a release-preparation pull request and require the complete CI matrix.
- Merge the preparation pull request to `master`. The release workflow rejects
  tags whose commit is not on `master`, whose event SHA does not identify the
  tag, or whose version differs from `CMakeLists.txt`.

CI must build, test, start, package, checksum, install, and run `--version` for:

- Linux `x86_64`
- Linux `arm64` on a native `aarch64` runner
- Linux `x86_64` AppImage built against the Debian 12 glibc baseline
- Linux `arm64` AppImage built against the Debian 12 glibc baseline
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
- Create an annotated `vX.Y.Z` tag on the tested `master` commit and push only
  that tag:

  ```bash
  git switch master
  git pull --ff-only
  git tag -a vX.Y.Z -m "Termify vX.Y.Z"
  git push origin vX.Y.Z
  ```

- Watch the **Release** workflow. It independently builds, tests, packages,
  installs, and checks `--version` for Linux x86_64, Linux ARM64, and both
  macOS architectures. The x86_64 Linux archive is produced in an Arch Linux
  container to preserve the documented FFmpeg ABI contract; the ARM64 archive
  is built natively on Ubuntu 24.04. Self-contained AppImages for both Linux
  architectures are produced separately on Debian 12, verify their bundled
  dependency closure, and start on clean native Ubuntu runners without
  requiring FUSE. Every package also requires the architecture-matched bundled
  yt-dlp to report the pinned version.
- The workflow creates keyless Sigstore-backed GitHub artifact attestations for
  every archive, per-archive checksum, and the aggregate `SHA256SUMS` manifest.
- Publishing is draft-first. The complete asset set is uploaded to a draft and
  the release is made public only by the final step. The workflow refuses to
  replace any existing release, including a draft. If a run is interrupted
  after draft creation, inspect and delete the incomplete draft before rerunning
  the failed workflow jobs.
- Generated notes are grouped using `.github/release.yml`. Maintainers should
  use the `enhancement`, `bug`, `documentation`, `dependencies`, and
  `breaking-change` pull-request labels; use `skip-changelog` only for changes
  that should not appear in user-facing notes.
- Keep the macOS signing/notarization limitation visible until signed release
  artifacts are part of the publishing workflow.

Do not move a published release tag or replace published assets. Enable GitHub's
immutable releases setting when available so this policy is enforced by the
repository.

## Verify a release

First verify that the checksum manifest and selected archive were produced by
Termify's tag-only release workflow. This uses GitHub CLI's Sigstore verifier
and the attestations stored by GitHub:

```bash
TAG=vX.Y.Z
REPOSITORY=segfaultcafe/Termify
WORKFLOW=segfaultcafe/Termify/.github/workflows/release.yml

gh attestation verify SHA256SUMS \
  --repo "$REPOSITORY" \
  --signer-workflow "$WORKFLOW" \
  --source-ref "refs/tags/$TAG"
gh attestation verify "termify-${TAG}-linux-x86_64.tar.gz" \
  --repo "$REPOSITORY" \
  --signer-workflow "$WORKFLOW" \
  --source-ref "refs/tags/$TAG"
gh attestation verify "termify-${TAG}-linux-x86_64.AppImage" \
  --repo "$REPOSITORY" \
  --signer-workflow "$WORKFLOW" \
  --source-ref "refs/tags/$TAG"
```

Replace `x86_64` with `arm64` to verify either Linux ARM64 artifact.

Then verify the archive bytes against the signed manifest:

```bash
sha256sum --check SHA256SUMS
```

On macOS, use `shasum -a 256 -c SHA256SUMS`. The per-artifact `.sha256` files
are also attested and can be verified with the same `gh attestation verify`
command before passing one to `sha256sum -c` or `shasum -a 256 -c`.

Artifact attestations prove which repository, workflow, tag ref, and commit
produced the bytes. They do not notarize the macOS executable. Online
attestation verification currently requires access to GitHub's attestation API;
the ordinary SHA-256 check remains available offline after the trusted manifest
has been obtained.

## Repository requirements

The release workflow uses only the short-lived `GITHUB_TOKEN` and GitHub's OIDC
identity. It needs `contents: write`, `id-token: write`, `attestations: write`,
and `artifact-metadata: write` permissions as declared by the publishing jobs.
No long-lived signing key or release token should be added. Artifact
attestations are available for this public repository; private forks may need
an eligible GitHub plan and adjusted Actions policy.
