# Architecture and engineering constraints

## Data and commands

`Project` owns media references and clips. Clip ranges are half-open integer frame intervals at 30 fps. The source in-point is independent of timeline start. Validation bounds all values to 24 hours, checks ID uniqueness, references and source extents, and rejects overlaps. Input video of other frame rates is mapped onto the project timebase; this is not a native variable-frame-rate editing model.

`Editor::command` edits a candidate copy, validates it, then commits it as one undoable change. Invalid commands emit a user-facing error without mutation. Import, relink, move, trim, split, delete, ripple delete and recovery all use this boundary. History stores Qt implicitly shared snapshots, capped at 250 entries. Larger projects will need compact delta commands and indexed interval lookup; current maximums are validation ceilings, not performance guarantees.

Saved state is a separate snapshot. Returning to that state via undo clears the dirty indicator. Autosave never changes saved state. Selection, seek and playback are transport/UI state rather than project edits.

## Playback

Qt Multimedia owns decoder output and audio synchronization within a source. `PlaybackController` maps decoded media position back into project frames. A wall clock advances gaps, clamping at the next clip boundary. Seeking uses integer-frame-to-millisecond conversion. Source audition pauses program playback, and program playback pauses the source.

Decoding and rendering are delegated to the available Qt backend. Cut boundaries require seek/source changes and can stall or have brief discontinuities. Frame stepping requests frame-aligned timestamps but exact decoded-frame selection is backend-dependent. This is **not** a custom frame-accurate, seamless, render-ahead engine. No GPU performance claim is made. A later engine should schedule decoded frames against an audio clock with bounded RAM/disk caches and proxy support.

## Export

The render task takes an immutable project snapshot. Each clip/gap becomes an independent 720p/30 fps segment. Video is normalized to square pixels with aspect-preserving letterboxing. Audio is stereo 48 kHz PCM in intermediate MKV files, with silence for missing audio. H.264 video is encoded per segment and copied into the final MP4 while PCM audio is encoded to AAC. This prioritizes predictable memory use and independently testable segments over render speed and disk use.

Segments and final intermediate output reside in a private `QTemporaryDir` at the destination. A generated concat manifest contains only generated basenames, never imported filenames. A final `QSaveFile` copies and atomically commits the completed file. Existing destinations survive canceled/failed renders. Peak working disk use includes all intermediate segments, intermediate output, and the atomic final copy. Progress is per-segment, not an estimated completion time. Only one export runs at a time; a persistent render queue comes later.

## Untrusted media and project files

- Project reads and writes are bounded to 16 MiB; schema, timebase, integers, IDs, paths and ranges are validated before mutation.
- Imports accept a self-contained audio/video extension allowlist. FFprobe uses a local-file protocol whitelist, an output bound and timeout. FFmpeg receives argument arrays through `QProcess`; there is no shell evaluation.
- Metadata is used as text, never interpreted as executable commands or filter expressions. User paths are passed as individual arguments.
- Decoder processes have cancellation/time limits but **are not OS-sandboxed**. Qt preview decodes in-process. Extension checks and a protocol whitelist do not make malicious media safe; isolated restricted decoding and fuzzing are required before a production release.
- Missing files can be loaded as offline references. Project and export UI reject overwriting source references. No plugin, AI model, downloaded script, network asset or update package is executed.
- SQLite, plugin SDK, updater, model service and network collaboration are deliberately absent until their phases.

## Remaining MVP limitations

One linked audiovisual track; fixed project/output settings; no still-image import; no waveform or thumbnail extraction; no source in/out marking; no standalone audio trim/mix; no transitions/effects/keyframes; no render queue; no proxy or GPU controls. Recovery has a 30-second window and one per-account slot. Large imports are sequential within a background batch, up to 100 files; close cancels the active probe and remaining batch. Timeline UI has a one-million-pixel width ceiling and linear clip lookup. Hardware audio output and interactive usability need testing on real Windows and Linux desktops. Qt backend codec support may differ from FFmpeg's support.

## References

The playback boundary follows the [Qt QMediaPlayer API](https://doc.qt.io/qt-6/qmediaplayer.html). Media normalization uses the documented [FFmpeg filters](https://ffmpeg.org/ffmpeg-filters.html) and CLI argument conventions. These are dependency interfaces, not copied application architecture or assets.
