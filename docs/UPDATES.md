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

This check does not weaken the release trust model. Before installing a future
update, Termify must verify the downloaded artifact against the attested
`SHA256SUMS` manifest described in [RELEASE.md](RELEASE.md). Automatic download,
atomic replacement, and rollback are separate milestones and are not performed
by `update --check`.
