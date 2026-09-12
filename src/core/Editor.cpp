#include "Editor.h"
#include <algorithm>
#include <stdexcept>
namespace sentinel {
namespace {
Clip& findClip(Project& p, const QString& id) {
    for (auto& c : p.clips)
        if (c.id == id)
            return c;
    throw std::runtime_error("Select a timeline clip first");
}
} // namespace
Editor::Editor(QObject* parent) : QObject(parent) {
}
void Editor::reset(Project p) {
    p.validate();
    std::sort(p.clips.begin(), p.clips.end(), [](const Clip& a, const Clip& b) { return a.start < b.start; });
    project_ = std::move(p);
    saved_ = project_;
    undo_.clear();
    redo_.clear();
    emit changed();
}
void Editor::markSaved() {
    saved_ = project_;
    emit savedStateChanged();
}
bool Editor::command(const QString& label, const std::function<void(Project&)>& edit) {
    try {
        Project next = project_;
        edit(next);
        next.validate();
        std::sort(next.clips.begin(), next.clips.end(),
                  [](const Clip& a, const Clip& b) { return a.start < b.start; });
        if (next == project_)
            return false;
        undo_.push_back({label, project_});
        if (undo_.size() > 250)
            undo_.removeFirst();
        redo_.clear();
        project_ = std::move(next);
        emit changed();
        return true;
    } catch (const std::exception& e) {
        emit error(QString::fromUtf8(e.what()));
        return false;
    }
}
bool Editor::importMedia(const QVector<Media>& media) {
    return command("Import media", [&](Project& p) {
        for (const auto& m : media) {
            bool duplicate = false;
            for (const auto& existing : p.media)
                if (existing.path == m.path)
                    duplicate = true;
            if (!duplicate)
                p.media.push_back(m);
        }
    });
}
bool Editor::addClip(const QString& id, Frame start) {
    return command("Add clip", [&](Project& p) {
        const auto* m = p.findMedia(id);
        if (!m)
            throw std::runtime_error("Media is no longer in this project");
        p.clips.push_back({newId(), id, start, 0, m->frames});
    });
}
bool Editor::split(const QString& id, Frame at) {
    return command("Split clip", [&](Project& p) {
        auto& c = findClip(p, id);
        if (at <= c.start || at >= c.end())
            throw std::runtime_error("Playhead must be inside the selected clip");
        Clip right = c;
        const Frame leftLength = at - c.start;
        right.id = newId();
        right.start = at;
        right.in += leftLength;
        right.length -= leftLength;
        c.length = leftLength;
        p.clips.push_back(right);
    });
}
bool Editor::remove(const QString& id, bool ripple) {
    return command(ripple ? "Ripple delete" : "Delete clip", [&](Project& p) {
        const Clip old = findClip(p, id);
        p.clips.erase(
            std::remove_if(p.clips.begin(), p.clips.end(), [&](const Clip& c) { return c.id == id; }),
            p.clips.end());
        if (ripple)
            for (auto& c : p.clips)
                if (c.start >= old.end())
                    c.start -= old.length;
    });
}
bool Editor::move(const QString& id, Frame start) {
    return command("Move clip", [&](Project& p) { findClip(p, id).start = start; });
}
bool Editor::trim(const QString& id, Frame start, Frame in, Frame length) {
    return command("Trim clip", [&](Project& p) {
        auto& c = findClip(p, id);
        c.start = start;
        c.in = in;
        c.length = length;
    });
}
void Editor::undo() {
    if (undo_.isEmpty())
        return;
    auto entry = undo_.takeLast();
    redo_.push_back({entry.label, project_});
    project_ = entry.project;
    emit changed();
}
void Editor::redo() {
    if (redo_.isEmpty())
        return;
    auto entry = redo_.takeLast();
    undo_.push_back({entry.label, project_});
    project_ = entry.project;
    emit changed();
}
} // namespace sentinel
