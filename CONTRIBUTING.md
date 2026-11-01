# Contributing to Termify

Thanks for helping make Termify a better free and open-source music player for
Linux. Contributions of code, tests, documentation, bug reports, and focused
feature proposals are welcome.

## Before you start

- Search existing issues and pull requests before starting overlapping work.
- For a substantial feature or architectural change, open an issue first so
  its scope and user experience can be agreed on before implementation.
- Keep changes focused. Unrelated cleanup makes reviews and regressions harder.
- Do not commit downloaded media, cache files, build output, credentials, or
  personal configuration.

Termify's supported-release goals and current milestones are documented in
[ROADMAP.md](ROADMAP.md). User-facing TUI changes should also follow
[docs/TUI.md](docs/TUI.md).

## Development setup

Termify currently targets Linux and requires:

- CMake 3.16 or newer
- A C++23-capable compiler
- `pkg-config`
- FFmpeg development libraries: `libavformat`, `libavcodec`, `libavutil`, and
  `libswresample`
- ALSA development libraries
- SQLite3 development libraries
- `curl` and `sha256sum`

Clone your fork and build the Debug target:

```bash
git clone https://github.com/YOUR-USERNAME/Termify.git
cd Termify
git remote add upstream https://github.com/segfaultcafe/Termify.git
make build
```

`make build` configures and compiles `bin/Debug`, downloads and verifies the
pinned standalone yt-dlp when it is not cached, and runs the test suite.

Useful commands:

```bash
make run        # run the Debug build
make test       # rebuild and run all tests with failure output
make clean      # remove the Debug build directory
make package    # build and verify a release archive and checksum
```

Do not use `make install` during ordinary development: it installs a tested
release system-wide under `/usr/local`.

## Branch and pull-request workflow

The `master` branch is protected and does not accept direct pushes.

1. Synchronize your fork with `upstream/master`.
2. Create a short-lived branch with a descriptive name, such as
   `fix/queue-order` or `feat/keyboard-seek`.
3. Make focused commits and add tests for behavior changes.
4. Run `make test` before pushing.
5. Push the branch to your fork and open a pull request against `master`.

GitHub Actions builds each pull request with GCC and Clang, runs the unit tests
under AddressSanitizer and UndefinedBehaviorSanitizer, checks shell syntax and
whitespace, and smoke-tests the release package. Test-result XML files are
retained as workflow artifacts for 14 days. All required checks must pass
before a pull request can be merged.

Maintainers may open pull requests from branches in the main repository.
Contributors without repository write access should always work from a fork.

In the pull request:

- Explain the problem and the chosen solution.
- Describe user-visible behavior and compatibility implications.
- List the tests you ran.
- Include a screenshot or short recording for visible TUI changes.
- Mention related issues with `Fixes #123` when appropriate.
- Keep review conversations resolved and update documentation when behavior or
  commands change.

Draft pull requests are welcome for early feedback, but please keep unfinished
or experimental work clearly marked.

## Commit messages

Use a concise, imperative subject. The repository generally uses a simple
Conventional Commit-style prefix:

```text
feat: add keyboard seeking
fix: preserve queue order after skip
docs: explain playlist cache behavior
test: cover duplicate play-next requests
refactor: isolate terminal input decoding
```

Keep commits logically complete. Avoid mixing formatting-only changes with a
functional change unless the formatting is limited to the lines being edited.

## C++ style

Match the surrounding code and keep every build warning-free under
`-Wall -Wextra -Wpedantic`.

- Use C++23 and prefer standard-library types, RAII, and value semantics.
- Indent with two spaces; do not use tabs.
- Put opening braces on the same line as declarations and control statements.
- Keep lines near 80 characters when practical and wrap long expressions in a
  way that preserves their structure.
- Use `PascalCase` for classes, structs, enums, and public methods.
- Use `camelCase` for local variables, parameters, and private helper
  functions.
- Prefix private data members with `_`, for example `_queueMtx`.
- Use scoped enums (`enum class`) and initialize members at their declaration
  when a clear default exists.
- Prefer `const`, `constexpr`, `std::optional`, smart pointers, and
  `std::lock_guard` over mutable globals, sentinel values, raw ownership, and
  manual locking.
- Use `auto` when the type is obvious from the expression; spell out a type
  when it communicates meaning.
- Put the matching project header first in a `.cpp` file, then a blank line,
  then standard or external headers.
- Keep code inside the appropriate `termify::app`, `termify::core`, or
  `termify::tui` namespace.
- Use the header-guard convention already present in the file's subsystem.
- Comments should explain constraints or intent, not restate the code.

There is currently no repository-wide automatic formatter. Do not reformat an
entire file for a small change; preserve the established local style.

## Architecture boundaries

Keep responsibilities separated:

- `src/App` coordinates application state, commands, and views.
- `src/Core` owns playback, decoding, downloading, queues, caching, and
  persistent storage.
- `src/Tui` owns terminal lifecycle, input decoding, drawing, focus, and
  reusable widgets.

Important invariants:

- Application code must not emit raw ANSI positioning, color, border, or mouse
  sequences. Render through `Canvas` and reusable widgets.
- A visible operation should remain usable with both keyboard and mouse when
  it is exposed as a TUI action.
- Widgets receive view data, not database or mixer ownership.
- Rendering must be clipped to widget bounds and remain safe at the documented
  minimum terminal size of 68 × 20.
- Database and network work must not run on the audio-output thread.
- Shared playback and UI state must respect the existing mutex and atomic
  ownership boundaries.
- Storage failures must not prevent ordinary playback.
- Playlist entries use canonical provider data and hashes; arbitrary typed
  queries are not trusted as persistent song metadata.
- Queue, cache, and playlist limits are deliberate. Do not make work unbounded
  without an explicit design discussion.
- Terminal modes, downloader processes, worker threads, temporary files, and
  cache leases must be cleaned up on success, cancellation, and failure.

Read [docs/TUI.md](docs/TUI.md) before changing widgets, focus behavior,
coordinates, pointer handling, or terminal input.

## Tests

Behavior changes require tests. The current suite covers:

- queue ordering and duplicate handling;
- reusable TUI primitives;
- SQLite history, cache, and playlist persistence;
- keyboard and mouse navigation;
- downloader/cache behavior; and
- mixer queue and playlist ordering.

Add a focused test to the closest existing test executable under `tests/`. If
a new subsystem needs its own executable, register it with CTest in
`CMakeLists.txt` and compile it with the same warning flags as the application.

Before opening a pull request, run:

```bash
make test
git diff --check
```

For packaging or installer changes, also run `make package`, verify the
generated checksum, and test the staged binary's `--version` output. Tests
must not depend on a user's real library, cache, network state, or audio
device; use isolated temporary paths and deterministic fixtures.

## Documentation and user-facing changes

Update the relevant documentation in the same pull request:

- `README.md` for installation, commands, and visible behavior;
- `docs/TUI.md` for interface architecture and interaction rules;
- `ROADMAP.md` when a milestone or supported-release goal changes; and
- `CMakeLists.txt` when preparing an intentional version change.

Keep CLI commands and clickable controls behaviorally consistent. Error and
status messages should be short, actionable, and readable in the terminal.

## Reporting bugs

A useful bug report includes:

- the Termify version (`termify --version`);
- Linux distribution, terminal, and terminal dimensions;
- FFmpeg and audio-stack details when playback is involved;
- exact reproduction steps and expected versus actual behavior;
- relevant terminal output, with credentials and personal paths removed; and
- whether the problem also occurs with a fresh cache or configuration.

Please do not include copyrighted audio, private playlist data, access tokens,
or full personal database files in an issue.

## License

By contributing, you agree that your contribution will be licensed under the
project's [MIT License](LICENSE).
