#include "MediaProbe.h"
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <cmath>
#include <stdexcept>
namespace sentinel {
QString MediaProbe::executable(const QString& name) {
    const auto path = QStandardPaths::findExecutable(name);
    if (path.isEmpty())
        throw std::runtime_error(
            (name + " is missing. Install FFmpeg and add its bin directory to PATH.").toStdString());
    return path;
}
Media MediaProbe::probe(const QString& path, const std::atomic_bool* cancel) {
    if (cancel && cancel->load())
        throw std::runtime_error("Import canceled");
    QFileInfo file(path);
    if (!file.isFile() || !file.isReadable())
        throw std::runtime_error("Media file is missing or unreadable");
    // Restrict the initial MVP to self-contained media; playlists can reference remote or private files.
    const QStringList extensions{"mp4", "mov", "mkv", "avi",  "mxf", "webm",
                                 "wav", "mp3", "aac", "flac", "m4a", "ogg"};
    if (!extensions.contains(file.suffix().toLower()))
        throw std::runtime_error(
            "Unsupported import type. Phase 1 supports video and audio files; still images come later.");
    QProcess process;
    process.setProgram(executable("ffprobe"));
    process.setArguments({"-v", "error", "-protocol_whitelist", "file", "-show_entries",
                          "format=duration:stream=codec_type,codec_name,width,height,duration", "-of", "json",
                          file.canonicalFilePath()});
    process.start();
    if (!process.waitForStarted(5000))
        throw std::runtime_error("Cannot start ffprobe");
    QElapsedTimer timer;
    timer.start();
    QByteArray output, errors;
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(100);
        output += process.readAllStandardOutput();
        errors += process.readAllStandardError();
        if ((cancel && cancel->load()) || timer.elapsed() > 30000 ||
            output.size() + errors.size() > 1024 * 1024) {
            process.kill();
            process.waitForFinished();
            throw std::runtime_error(cancel && cancel->load()
                                         ? "Import canceled"
                                         : "Media probe exceeded its time or output limit");
        }
    }
    output += process.readAllStandardOutput();
    errors += process.readAllStandardError();
    if (cancel && cancel->load())
        throw std::runtime_error("Import canceled");
    if (output.size() + errors.size() > 1024 * 1024)
        throw std::runtime_error("Media probe exceeded its output limit");
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        throw std::runtime_error(
            ("Cannot decode media: " + QString::fromUtf8(errors).left(500)).toStdString());
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(output, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
        throw std::runtime_error("Invalid ffprobe response");
    const auto root = doc.object();
    double seconds = root["format"].toObject()["duration"].toString().toDouble();
    Media m;
    m.id = newId();
    m.path = file.canonicalFilePath();
    m.name = file.fileName();
    for (const auto& value : root["streams"].toArray()) {
        const auto stream = value.toObject();
        const auto type = stream["codec_type"].toString();
        seconds = std::max(seconds, stream["duration"].toString().toDouble());
        if (type == "video" && !m.video) {
            m.video = true;
            m.width = stream["width"].toInt();
            m.height = stream["height"].toInt();
            m.codec = stream["codec_name"].toString();
        }
        if (type == "audio") {
            m.audio = true;
            if (m.codec.isEmpty())
                m.codec = stream["codec_name"].toString();
        }
    }
    if (!std::isfinite(seconds) || seconds < 1.0 / kFps || seconds > 86400)
        throw std::runtime_error("Media must have a known duration between one frame and 24 hours");
    m.frames = static_cast<Frame>(std::floor(seconds * kFps + 0.0001));
    Project validation;
    validation.media.push_back(m);
    validation.validate();
    return m;
}
} // namespace sentinel
