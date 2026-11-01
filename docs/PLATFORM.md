# Platform architecture

Termify keeps operating-system details behind capability interfaces declared in
`src/Platform/Services.hpp`. Application, Core, and TUI code must not select an
operating system with preprocessor macros or include native OS APIs directly.

## Design

The architecture uses inheritance for narrow capability contracts and
composition for a complete platform:

- `ProcessRunner` starts, waits for, cancels, and times out child processes.
- `FileSystem` owns executable discovery, user directories, temporary paths,
  and advisory file leases.
- `TerminalDriver` owns raw terminal mode, byte input, terminal dimensions,
  and restoration.
- `AudioOutput` accepts Termify's decoded interleaved signed 16-bit PCM frames.
- `PlatformServices` composes those capabilities and installs the native
  interrupt behavior.

`CurrentPlatform()` is the composition root. Exactly one OS implementation
defines it in each build. Core code sees only the interfaces; CMake is the one
place that chooses the implementation sources and native libraries.

Shared POSIX behavior lives in `src/Platform/Posix`. Linux-specific executable
discovery and ALSA output live in `src/Platform/Linux`. POSIX code is reusable
by macOS only where the semantics are genuinely shared; macOS-specific paths,
audio, packaging, and integration belong in `src/Platform/MacOS`.

## Porting an operating system

1. Implement each capability needed by `PlatformServices`. Reuse a shared
   implementation through composition where its behavior matches the target.
2. Add one OS composition root that owns its concrete capability objects and
   defines `CurrentPlatform()`.
3. Add the implementation sources and native libraries to the platform block
   in `CMakeLists.txt`. Do not add OS-selection macros to consumers.
4. Keep process cancellation, terminal restoration, cache leases, and audio
   reset behavior equivalent to the existing Linux implementation.
5. Run the complete test suite on every supported architecture and add native
   tests for the new implementations.

The macOS port targets both Intel (`x86_64`, also called AMD64) and Apple
Silicon (`arm64`). The initial implementation should use the shared POSIX
process and terminal capabilities, provide macOS filesystem conventions and
executable discovery, and add a native Core Audio output implementation.

Windows remains a later target. Its implementation can satisfy the same
interfaces without emulating POSIX or changing Core code.

## Integration coverage

Pull requests build and run the complete test suite, including the application
startup smoke test, in Arch Linux, Debian, and Ubuntu containers. macOS cannot
be represented by a Linux container, so CI uses native GitHub-hosted machines:

- `macos-15-intel` validates the `x86_64`/AMD64 build.
- `macos-15` validates the Apple Silicon `arm64` build.

The startup smoke test runs the real non-interactive application, feeds it the
`exit` command, and requires a clean exit. This covers platform composition,
storage initialization, downloader discovery, mixer construction, and command
processing without requiring an audio device or network access.

## Boundary rule

Native headers, constants, system calls, and platform libraries are allowed
only below `src/Platform`. A capability missing from the interfaces should be
added as a small platform-neutral operation rather than exposed as a native
handle. Build-time platform checks belong in `CMakeLists.txt`.
