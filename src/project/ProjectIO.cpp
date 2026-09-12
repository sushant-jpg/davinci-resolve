#include "ProjectIO.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace sentinel {
namespace {
constexpr qint64 maxBytes = 16 * 1024 * 1024;
void fail(const QString& msg) {
    throw std::runtime_error(msg.toStdString());
}
QString string(const QJsonObject& o, const char* key) {
    if (!o[key].isString())
        fail(QString("Missing string: %1").arg(key));
    return o[key].toString();
}
qint64 number(const QJsonObject& o, const char* key, qint64 max = kMaxFrames) {
    const auto v = o[key];
    const double n = v.toDouble(-1);
    if (!v.isDouble() || !std::isfinite(n) || n < 0 || n > max || std::floor(n) != n)
        fail(QString("Invalid integer: %1").arg(key));
    return static_cast<qint64>(n);
}
bool boolean(const QJsonObject& o, const char* key) {
    if (!o[key].isBool())
        fail(QString("Missing boolean: %1").arg(key));
    return o[key].toBool();
}
void atomicWrite(const QString& path, const QByteArray& data) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        fail("Cannot save " + path + ": " + file.errorString());
}
} // namespace
QByteArray ProjectIO::encode(const Project& p) {
    p.validate();
    QJsonArray media, clips;
    for (const auto& m : p.media)
        media.append(QJsonObject{{"id", m.id},
                                 {"path", m.path},
                                 {"name", m.name},
                                 {"frames", m.frames},
                                 {"video", m.video},
                                 {"audio", m.audio},
                                 {"width", m.width},
                                 {"height", m.height},
                                 {"codec", m.codec}});
    for (const auto& c : p.clips)
        clips.append(QJsonObject{
            {"id", c.id}, {"mediaId", c.mediaId}, {"start", c.start}, {"in", c.in}, {"length", c.length}});
    auto bytes = QJsonDocument(QJsonObject{{"format", "sentinel-project"},
                                           {"version", 1},
                                           {"fps", kFps},
                                           {"name", p.name},
                                           {"media", media},
                                           {"clips", clips}})
                     .toJson();
    if (bytes.size() > maxBytes)
        fail("Project exceeds 16 MiB limit");
    return bytes;
}
Project ProjectIO::decode(const QByteArray& bytes) {
    if (bytes.size() > maxBytes)
        fail("Project exceeds 16 MiB limit");
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        fail("Invalid project JSON");
    const auto o = doc.object();
    if (string(o, "format") != "sentinel-project" || number(o, "version") != 1 || number(o, "fps") != kFps)
        fail("Unsupported project version or frame rate");
    if (!o["media"].isArray() || !o["clips"].isArray())
        fail("Missing project collections");
    if (o["media"].toArray().size() > 10000 || o["clips"].toArray().size() > 10000)
        fail("Project exceeds item limits");
    Project p;
    p.name = string(o, "name");
    for (const auto& v : o["media"].toArray()) {
        if (!v.isObject())
            fail("Invalid media entry");
        const auto m = v.toObject();
        p.media.push_back({string(m, "id"), string(m, "path"), string(m, "name"), number(m, "frames"),
                           boolean(m, "video"), boolean(m, "audio"), int(number(m, "width", 32768)),
                           int(number(m, "height", 32768)), string(m, "codec")});
    }
    for (const auto& v : o["clips"].toArray()) {
        if (!v.isObject())
            fail("Invalid clip entry");
        const auto c = v.toObject();
        p.clips.push_back({string(c, "id"), string(c, "mediaId"), number(c, "start"), number(c, "in"),
                           number(c, "length")});
    }
    p.validate();
    std::sort(p.clips.begin(), p.clips.end(), [](const Clip& a, const Clip& b) { return a.start < b.start; });
    return p;
}
Project ProjectIO::load(const QString& path) {
    if (!QFileInfo(path).isFile())
        fail("Project path is not a regular file");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(file.errorString());
    if (file.size() > maxBytes)
        fail("Project exceeds 16 MiB limit");
    return decode(file.read(maxBytes + 1));
}
void ProjectIO::save(const Project& p, const QString& path, bool backup) {
    const auto bytes = encode(p);
    QStringList destinations{path};
    if (backup && QFileInfo::exists(path))
        destinations << path + ".bak";
    for (const auto& destination : destinations) {
        const QFileInfo target(destination);
        for (const auto& media : p.media) {
            const QFileInfo source(media.path);
            if (source.absoluteFilePath() == target.absoluteFilePath() ||
                (!target.canonicalFilePath().isEmpty() &&
                 source.canonicalFilePath() == target.canonicalFilePath()))
                fail("Project or backup destination cannot overwrite source media");
        }
    }
    if (backup && QFileInfo::exists(path)) {
        QFile old(path);
        if (!old.open(QIODevice::ReadOnly) || old.size() > maxBytes)
            fail("Cannot back up existing project");
        const auto previous = old.read(maxBytes + 1);
        if (old.error() != QFileDevice::NoError || previous.size() > maxBytes)
            fail("Cannot read previous project for backup");
        atomicWrite(path + ".bak", previous);
    }
    atomicWrite(path, bytes);
}
} // namespace sentinel
