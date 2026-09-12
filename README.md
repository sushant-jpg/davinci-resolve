# Sentinel Studio

An original C++20 / Qt 6 desktop video editor. This repository implements a **Phase 1 MVP**, not the complete professional editor described in the long-term roadmap. No proprietary code, branding, icons, or assets are used.

## Build and run

Requirements: CMake 3.20+, a C++20 compiler, Qt 6.4+ with Widgets, Multimedia, MultimediaWidgets, Concurrent and Test; FFmpeg and ffprobe on `PATH` with `libx264` and AAC encoders.

Debian / Ubuntu / Kali:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake qt6-base-dev qt6-multimedia-dev ffmpeg
cmake --preset debug
cmake --build --preset debug -j 4
ctest --preset debug
./scripts/run.sh
```

The current workspace also has an ignored `.local-deps` runtime cache for this Linux machine, so the already-built application can be launched with `./scripts/run.sh` without sudo. This cache is not a portable distribution and is not committed. The build toolchain used for verification was unpacked under `/tmp/sentinel-deps`.

On Windows 11, install Visual Studio 2022 C++ tools, CMake, and a matching Qt 6 MSVC kit with Qt Multimedia. Add the Qt kit and FFmpeg `bin` directories to `PATH`, then build from an x64 developer terminal:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
.\build\Debug\sentinel-studio.exe
```

Adjust the example Qt path to your installed kit. Windows is an intended target; it has not yet been tested. Redistribution needs Qt platform/multimedia plugins and codec runtime libraries; `cmake --install` alone does not produce a self-contained distribution. Review dependency licenses before distributing binaries.

## Editing workflow

1. Create a named project with **Project → New project**, or start in the untitled sequence.
2. Import video/audio with **Ctrl+I**, the toolbar, or file drag/drop. Import is asynchronous; unsupported or malformed files produce errors.
3. Double-click bin media to play in the source viewer. Use its slider to seek.
4. Drag media from the bin to an empty timeline range, or use **Append to timeline**. Original audio stays linked to its video. Audio-only media uses black video.
5. Click the ruler to seek; click a clip to select it. Drag the body to move, or either edge to trim. Enable **Snap** to align starts/ends to clip boundaries and the playhead. Invalid overlapping edits are rejected.
6. Use the inspector for exact **timeline start**, **source in**, and **duration**, measured in 30 fps frames. Click **Apply clip range**.
7. Press **Space** to play/pause, **Left/Right** to step, **Home** to go to the start, **S** to split the selected clip at the playhead, **Delete** to lift a clip, or **Shift+Delete** to ripple delete.
8. **Ctrl+Z / redo** undo or redo up to 250 project edits. Customize bindings under **Edit → Keyboard shortcuts**. Layouts persist; panels dock, float, resize and can be restored from **View**.
9. Save to `.sentinel` with **Ctrl+S**, load with **Ctrl+O**, or use recent projects. The document references external media; it never embeds raw video.
10. Export using **Ctrl+M**: H.264/AAC MP4, 1280×720, 30 fps, original media. Render runs in the background and can be canceled. Timeline gaps become black/silence; video-only sources receive silence. The destination is replaced only after successful rendering.

## Persistence and recovery

Normal saves use an atomic replacement and retain the previous file as `.bak`. Unsaved edits are periodically written to a recovery snapshot in Qt's application data directory, with one previous snapshot retained. Use **Project → Recover autosave** to restore it, then save under a project filename. A malformed load leaves the current project intact. Missing sources remain listed as **OFFLINE** and can be relinked; replacements must be long enough for all existing edits.

The recovery interval is 30 seconds; edits after the last snapshot can be lost. There is one recovery slot per OS account, so concurrent application instances are not yet supported for recovery. Autosave does not mark a project as saved.

## Architecture and tests

- `src/core`: validated project model and controlled undo/redo command boundary.
- `src/project`: bounded, versioned JSON and atomic persistence.
- `src/media`: local media metadata probing through FFprobe.
- `src/playback`: timeline-to-source mapping, playback, gaps and original audio.
- `src/export`: isolated FFmpeg processes, normalized segments, cancellation and atomic output commit.
- `src/ui`: dockable editing workspace, timeline gestures, media tasks and project lifecycle.
- `tests`: edit regression, project validation/persistence/recovery, generated-media probe/export checks, decoded pixel/audio checks, and offscreen desktop/playback smoke tests.

Tests require FFmpeg; missing dependencies fail the integration checks rather than silently skipping them. For a headless application smoke test:

```sh
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=generic ./scripts/run.sh --smoke-test
```

See [the initial assessment and plan](docs/IMPLEMENTATION.md), [verification results](docs/VERIFICATION.md), and [architecture/limitations](docs/ARCHITECTURE.md).

## Coming later

Phase 2: multitrack timeline, transitions, effects, keyframes, proxy workflows and measured hardware acceleration. Later phases: color management and grading, node compositing, motion graphics, advanced audio, local AI, plugin SDK and collaboration. These features are explicitly labeled as future work in the UI.
