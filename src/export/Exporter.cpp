#include "Exporter.h"
#include "media/MediaProbe.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <algorithm>
#include <stdexcept>
namespace sentinel {
namespace {
void fail(const QString& text) {
    throw std::runtime_error(text.toStdString());
}
QString seconds(Frame frames) {
    return QString::number(double(frames) / kFps, 'f', 9);
}
void run(const QString& ffmpeg, const QStringList& args, const QString& cwd, const std::atomic_bool& cancel) {
    QProcess p;
    p.setWorkingDirectory(cwd);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(ffmpeg, {QStringList{"-nostdin", "-hide_banner", "-loglevel", "error", "-y"} + args});
    if (!p.waitForStarted(5000))
        fail("Cannot start FFmpeg");
    QByteArray log;
    QElapsedTimer idle;
    idle.start();
    while (p.state() != QProcess::NotRunning) {
        p.waitForFinished(100);
        const auto bytes = p.readAll();
        if (!bytes.isEmpty())
            idle.restart();
        log = (log + bytes).right(8192);
        if (cancel.load() || idle.elapsed() > 30 * 60 * 1000) {
            p.kill();
            p.waitForFinished();
            fail(cancel.load() ? "Export canceled" : "FFmpeg timed out");
        }
    }
    log = (log + p.readAll()).right(8192);
    if (cancel.load())
        fail("Export canceled");
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        fail("FFmpeg export failed:\n" + QString::fromUtf8(log));
}
} // namespace
void Exporter::render(const Project& project, const QString& destination, const std::atomic_bool& cancel,
                      const std::function<void(int, const QString&)>& progress) {
    project.validate();
    if (project.clips.isEmpty())
        fail("The timeline is empty");
    const QFileInfo output(destination);
    if (output.suffix().toLower() != "mp4")
        fail("Phase 1 exports .mp4 files");
    for (const auto& m : project.media) {
        if (QFileInfo(m.path).absoluteFilePath() == output.absoluteFilePath() ||
            (!output.canonicalFilePath().isEmpty() &&
             QFileInfo(m.path).canonicalFilePath() == output.canonicalFilePath()))
            fail("Export destination cannot overwrite source media");
    }
    const QString ffmpeg = MediaProbe::executable("ffmpeg");
    QTemporaryDir temporary(QDir(output.absolutePath()).filePath(".sentinel-render-XXXXXX"));
    if (!temporary.isValid())
        fail("Cannot create temporary render directory at destination");
    QVector<Clip> clips = project.clips;
    std::sort(clips.begin(), clips.end(), [](const Clip& a, const Clip& b) { return a.start < b.start; });
    QVector<Clip> segments;
    Frame cursor = 0;
    for (const auto& c : clips) {
        if (c.start > cursor)
            segments.push_back({{}, {}, cursor, 0, c.start - cursor});
        segments.push_back(c);
        cursor = c.end();
    }
    QByteArray manifest;
    for (qsizetype i = 0; i < segments.size(); ++i) {
        if (cancel.load())
            fail("Export canceled");
        const auto& s = segments[i];
        const auto* m = project.findMedia(s.mediaId);
        if (m && (!QFileInfo(m->path).isFile() || !QFileInfo(m->path).isReadable()))
            fail("Missing media: " + m->path);
        if (progress)
            progress(int(i * 90 / segments.size()),
                     "Rendering segment " + QString::number(i + 1) + "/" + QString::number(segments.size()));
        QStringList args;
        int nextInput = 0;
        int videoInput = -1, audioInput = -1;
        if (m) {
            args << "-protocol_whitelist" << "file" << "-ss" << seconds(s.in) << "-i" << m->path;
            if (m->video)
                videoInput = nextInput;
            if (m->audio)
                audioInput = nextInput;
            ++nextInput;
        }
        if (videoInput < 0) {
            videoInput = nextInput++;
            args << "-f" << "lavfi" << "-i" << "color=c=black:s=1280x720:r=30";
        }
        if (audioInput < 0) {
            audioInput = nextInput++;
            args << "-f" << "lavfi" << "-i" << "anullsrc=r=48000:cl=stereo";
        }
        const QString filename = QString("segment-%1.mkv").arg(i);
        args << "-map" << QString::number(videoInput) + ":v:0" << "-map"
             << QString::number(audioInput) + ":a:0" << "-vf"
             << "scale=1280:720:force_original_aspect_ratio=decrease,pad=1280:720:(ow-iw)/2:(oh-ih)/"
                "2,setsar=1,fps=30,tpad=stop_mode=clone:stop_duration=86400"
             << "-af" << "aresample=48000,apad" << "-t" << seconds(s.length) << "-c:v" << "libx264"
             << "-preset" << "veryfast" << "-crf" << "18" << "-pix_fmt" << "yuv420p"
             << "-threads" << "2" << "-filter_threads" << "1" << "-c:a" << "pcm_s16le" << "-ar" << "48000"
             << "-ac" << "2" << filename;
        run(ffmpeg, args, temporary.path(), cancel);
        manifest += "file '" + filename.toUtf8() + "'\n";
    }
    QFile list(temporary.filePath("segments.txt"));
    if (!list.open(QIODevice::WriteOnly) || list.write(manifest) != manifest.size())
        fail("Cannot write render manifest");
    list.close();
    if (progress)
        progress(92, "Encoding audio and finalizing MP4");
    run(ffmpeg, {"-f",        "concat",     "-safe",     "1",     "-i",   "segments.txt",
                 "-map",      "0:v:0",      "-map",      "0:a:0", "-c:v", "copy",
                 "-c:a",      "aac",        "-b:a",      "192k",  "-t",   seconds(project.duration()),
                 "-movflags", "+faststart", "output.mp4"},
        temporary.path(), cancel);
    QFile rendered(temporary.filePath("output.mp4"));
    QSaveFile final(output.absoluteFilePath());
    if (!rendered.open(QIODevice::ReadOnly) || !final.open(QIODevice::WriteOnly))
        fail("Cannot commit rendered file");
    while (!rendered.atEnd()) {
        if (cancel.load())
            fail("Export canceled");
        const auto data = rendered.read(1024 * 1024);
        if (data.isEmpty() && rendered.error() != QFileDevice::NoError)
            fail("Error reading rendered file");
        if (final.write(data) != data.size())
            fail("Error writing export; check disk space");
    }
    if (cancel.load())
        fail("Export canceled");
    if (!final.commit())
        fail("Cannot atomically commit exported file");
    if (progress)
        progress(100, "Export complete");
}
} // namespace sentinel
