# Sentinel Studio: repository assessment and Phase 1 plan

## Initial assessment
The repository initially contained only a one-line README and Git metadata. There was no architecture, source, build system, dependency manifest, test suite, or existing feature to preserve. Consequently there were no existing broken functions, duplicates, application vulnerabilities, or measured performance bottlenecks. The local environment has GCC and Python but initially lacks CMake, Qt 6 development packages, FFmpeg, and ffprobe (Qt and codec runtime libraries were already present). System dependency installation requires credentials unavailable to the agent.

## Architecture and boundaries
- C++20 core: integer frame positions, single sequential audiovisual track with gaps, validated source ranges and collision rejection. Video and its original audio remain linked in Phase 1.
- Qt 6 Widgets: original dark desktop shell, dockable source/media/inspector panels, custom timeline with drag/drop, trimming and frame navigation. Qt Multimedia handles source/program playback and audio.
- Controlled snapshot commands: all project mutations pass through undo/redo history. Save cleanliness is tracked separately from autosave.
- Versioned JSON project codec: bounded reads, strict validation, external media references, atomic writes, previous-save backup and separate autosave recovery.
- FFprobe imports in a background task, subprocess argument arrays (no shell), local files only, bounded duration/output/timeouts; media failure never replaces the current project.
- FFmpeg export worker: sequential normalized segments including black/silent gaps, original audio or generated silence, cancelable subprocesses and atomic destination commit. Rendering is CPU-based in Phase 1. Export is based on an immutable project snapshot.
- Qt media decoding remains in-process; process-isolated decoding, native libav/GPU frame scheduling and hardened plugin hosting are future work.

## Folder and dependency plan
`src/{app,core,project,media,playback,export,ui}`, `tests`, `scripts`, `assets`, `docs`. Add other modules when implemented, rather than populate empty future modules. Build with CMake >=3.20, C++20 compiler, Qt >=6.4 Core/Widgets/Multimedia/MultimediaWidgets/Concurrent/Test. Runtime FFmpeg and ffprobe must be installed and discoverable on PATH. No Python runtime, AI dependencies, Vulkan or CUDA are needed for this CPU MVP.

## Implementation sequence / files
1. CMakeLists.txt, presets and README: reproducible build, tests, setup instructions.
2. core/Project and core/Editor: validated timeline, move/trim/split/delete, undo/redo and import.
3. project/ProjectIO: versioned save/load, atomic persistence and recovery.
4. media/MediaProbe: asynchronous FFprobe metadata and import validation.
5. export/Exporter: executable FFmpeg render pipeline with progress and cancellation.
6. playback/PlaybackController: source mapping, clip boundaries, gaps and audio playback.
7. ui/TimelineWidget, ui/MainWindow, app/main: functional editing workflow, dock layout, inspector, source viewer, persistence and export.
8. tests: core/edit regression, invalid projects, save/load, media probe, generated media export and offscreen UI smoke test.
9. Verify compilation, warnings, automated tests, generated audiovisual import/render, missing paths, recovery and desktop startup. Document any verification that cannot be completed.

## Scope
Phase 1 implements one video/audio-linked timeline, 30 fps project timebase, 1280x720 export, basic source/program playback and H.264/AAC MP4 export. No multitrack mixing, compositing, effects, AI, professional color, proxies, HDR, network collaboration, GPU guarantees or plugin execution are claimed. Subsequent phases follow the user's roadmap. Qt seek precision and codec availability depend on the platform backend; this MVP does not claim production-grade frame-accurate playback or seamless cut playback.
