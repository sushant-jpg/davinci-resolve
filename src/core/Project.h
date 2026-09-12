#pragma once
#include <QString>
#include <QVector>
#include <optional>

namespace sentinel {
using Frame = qint64;
constexpr int kFps = 30;
constexpr Frame kMaxFrames = 30LL * 60 * 60 * 24;
struct Media {
    QString id, path, name;
    Frame frames = 0;
    bool video = false, audio = false;
    int width = 0, height = 0;
    QString codec;
    bool operator==(const Media&) const = default;
};
struct Clip {
    QString id, mediaId;
    Frame start = 0, in = 0, length = 0;
    Frame end() const { return start + length; }
    bool operator==(const Clip&) const = default;
};
struct Project {
    QString name = "Untitled";
    QVector<Media> media;
    QVector<Clip> clips;
    Frame duration() const;
    const Media* findMedia(const QString& id) const;
    const Clip* clipAt(Frame frame) const;
    void validate() const;
    bool operator==(const Project&) const = default;
};
QString newId();
QString timecode(Frame frame);
} // namespace sentinel
