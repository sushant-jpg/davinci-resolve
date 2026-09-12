# Phase 1 verification report

Verified on 2026-09-12 in the supplied Kali Linux workspace. Initial repository: README only. There were no existing application features to modify or remove.

## Build and dependencies

- GCC 15.3, C++20, CMake 4.3.4, Qt 6.10.2, FFmpeg/ffprobe 8.1.2.
- CMake Debug configure and complete application/test build succeeded.
- Application targets compile with `-Wall -Wextra -Wpedantic`; final build emitted no compiler warnings.
- Dependencies were downloaded as Debian packages and unpacked into `/tmp/sentinel-deps`, without installing system packages. Existing host Qt/codec libraries are also used.
- A small, ignored `.local-deps` runtime cache supplies FFmpeg/ffprobe and the missing Qt Concurrent/MultimediaWidgets shared libraries to `scripts/run.sh` on this machine. The cache and build outputs are not committed and are not a portable installer.
- Added Linux CI configuration; the hosted GitHub workflow itself has not been run in this session.

## Tests performed

Both CTest suites passed, with no skipped tests:

| Suite | Evidence |
| --- | --- |
| Engine | Split, move, trim, delete, ripple delete, undo/redo, saved-state tracking, invalid edit rollback, duplicate import, bounds and reference validation |
| Project persistence | Unicode JSON round trip, actual atomic filesystem saves, previous-save backup, recovery snapshot load, malformed/truncated/oversized/version-mismatched inputs |
| Media and render integration (engine suite) | FFmpeg-generated audiovisual, video-only and audio-only sources; FFprobe import; mixed frame rates/resolutions; source in-points; black/silent timeline gaps; actual H.264/AAC MP4 output |
| Decoded render checks (engine suite) | Red/blue center pixels in expected clips, black pixels in the gap, nonzero decoded PCM audio, zero PCM in the silent gap, output size and duration |
| Failure paths (engine suite) | Missing/malformed media, canceled render, source overwrite rejection, and preservation of existing output after render failure |
| Desktop | Four docks, real timeline MIME drop, valid decoded frame while paused, playback progression, frame stepping, seek, keyboard split/delete, undo, clip body drag, both trim handles, and playback through a gap into the next clip and final stop |

Headless Qt initially failed because the inherited desktop theme tried to open a display. The CTest desktop environment now explicitly selects offscreen rendering and a generic theme. Restricted sandbox access also stalled PulseAudio initialization; desktop verification ran with host audio-service access. Rendered audio samples were verified automatically; subjective listening quality was not assessed.

## Visual and resource checks

The actual built application was launched through `scripts/run.sh` on a private Xvfb X11 display, opening a generated 720p audiovisual project. It rendered a paused program frame, displayed media and timeline clips, and exited normally after capture. All source controls were visible in the final default layout. See [the desktop capture](desktop-preview.png).

Peak sampled resident memory for that short startup/paused-preview session: **263.9 MiB**, sampled from `/proc` every 50 ms. This is a single sample, not a leak test or a high-resolution/long-duration benchmark. No crashes occurred in the successful build, regression, and desktop verification runs.

`git diff --check`, a trailing-whitespace scan of new text files, and `sh -n scripts/run.sh` passed.

## Issues corrected during implementation

- Paused program viewer did not initially decode a frame: prime video decoding with audio muted, then pause when a valid frame arrives.
- Default dock allocation clipped source controls: corrected panel minimum sizes and vertical sizing policies, then visually verified on X11.
- Pending decoder load could restart an old source after seeking into a gap: loaded-media handling now requires an active clip.
- Background import could delay shutdown for an entire batch: cancel the active probe and remaining files on destruction/project replacement.
- Inspector/error text now uses plain text rather than interpreting imported text as rich text.
- Fixed two compiler indentation warnings and a missing test include before the final successful build.

## Files created / modified

Modified: `README.md` (the only original content file).

Created:

- `.gitignore`, `.clang-format`, `CMakeLists.txt`, `CMakePresets.json`, `.github/workflows/build.yml`.
- `src/core/Project.{h,cpp}`, `src/core/Editor.{h,cpp}`.
- `src/project/ProjectIO.{h,cpp}`, `src/media/MediaProbe.{h,cpp}`.
- `src/export/Exporter.{h,cpp}`, `src/playback/PlaybackController.{h,cpp}`.
- `src/ui/TimelineWidget.{h,cpp}`, `src/ui/MainWindow.{h,cpp}`, `src/ui/MainWindowMedia.cpp`, `src/ui/MainWindowProject.cpp`.
- `src/app/main.cpp`, `tests/EngineTests.cpp`, `tests/UiTests.cpp`, `scripts/run.sh`.
- `assets/sentinel-studio.svg`, `assets/sentinel-studio.desktop`.
- `docs/IMPLEMENTATION.md`, `docs/ARCHITECTURE.md`, `docs/VERIFICATION.md`, `docs/desktop-preview.png`.

## Remaining limits and next phase

This is a working Phase 1 MVP, not a production-grade replacement for established professional editors. Windows builds, packaged deployment, physical multi-monitor use, long-session stability, subjective audio playback and high-resolution performance remain unverified. Playback accuracy depends on the Qt backend; cut boundaries can stall. Recovery can lose the most recent 30 seconds. Media decoding is not OS-sandboxed.

Next: Phase 2 multitrack data model and audio-clock playback scheduler, then effects/transitions/keyframes, proxies, hardware capability detection and repeatable performance benchmarks. See `ARCHITECTURE.md` for the complete current limitations.
