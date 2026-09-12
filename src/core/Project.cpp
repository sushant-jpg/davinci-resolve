#include "Project.h"
#include <QFileInfo>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <stdexcept>
namespace sentinel {
QString newId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
Frame Project::duration() const {
    Frame end = 0;
    for (const auto& c : clips)
        end = std::max(end, c.end());
    return end;
}
const Media* Project::findMedia(const QString& id) const {
    for (const auto& m : media)
        if (m.id == id)
            return &m;
    return nullptr;
}
const Clip* Project::clipAt(Frame f) const {
    for (const auto& c : clips)
        if (f >= c.start && f < c.end())
            return &c;
    return nullptr;
}
void Project::validate() const {
    auto require = [](bool ok, const char* msg) {
        if (!ok)
            throw std::runtime_error(msg);
    };
    require(!name.trimmed().isEmpty() && name.size() <= 256, "Invalid project name");
    require(media.size() <= 10000 && clips.size() <= 10000, "Project exceeds 10,000 item limit");
    QSet<QString> ids;
    for (const auto& m : media) {
        require(!m.id.isEmpty() && m.id.size() <= 128 && !ids.contains(m.id),
                "Duplicate or invalid media ID");
        ids.insert(m.id);
        require(!m.path.contains(QChar(0)) && m.path.size() <= 32768 && QFileInfo(m.path).isAbsolute(),
                "Media must reference an absolute local path");
        require(m.frames > 0 && m.frames <= kMaxFrames && (m.video || m.audio),
                "Invalid media duration or streams");
        require(m.width >= 0 && m.height >= 0 && m.width <= 32768 && m.height <= 32768,
                "Invalid media dimensions");
    }
    ids.clear();
    QVector<Clip> sorted = clips;
    std::sort(sorted.begin(), sorted.end(), [](const Clip& a, const Clip& b) { return a.start < b.start; });
    Frame end = 0;
    for (const auto& c : sorted) {
        require(!c.id.isEmpty() && c.id.size() <= 128 && !ids.contains(c.id), "Duplicate or invalid clip ID");
        ids.insert(c.id);
        const auto* m = findMedia(c.mediaId);
        require(m != nullptr, "Clip references unknown media");
        require(c.start >= 0 && c.start <= kMaxFrames && c.in >= 0 && c.in <= kMaxFrames && c.length > 0 &&
                    c.length <= kMaxFrames,
                "Invalid clip range");
        require(c.in + c.length <= m->frames && c.end() <= kMaxFrames,
                "Clip exceeds media or timeline bounds");
        require(c.start >= end, "Clips overlap on the Phase 1 track");
        end = c.end();
    }
}
QString timecode(Frame f) {
    f = std::max<Frame>(0, f);
    return QString("%1:%2:%3:%4")
        .arg(f / (kFps * 3600), 2, 10, QChar('0'))
        .arg((f / (kFps * 60)) % 60, 2, 10, QChar('0'))
        .arg((f / kFps) % 60, 2, 10, QChar('0'))
        .arg(f % kFps, 2, 10, QChar('0'));
}
} // namespace sentinel
