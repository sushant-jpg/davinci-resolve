#pragma once
#include "Project.h"
#include <QObject>
#include <functional>
namespace sentinel {
class Editor : public QObject {
    Q_OBJECT
  public:
    explicit Editor(QObject* parent = nullptr);
    const Project& project() const { return project_; }
    bool dirty() const { return !(project_ == saved_); }
    bool canUndo() const { return !undo_.isEmpty(); }
    bool canRedo() const { return !redo_.isEmpty(); }
    void reset(Project project = {});
    void markSaved();
    bool command(const QString& label, const std::function<void(Project&)>& edit);
    bool importMedia(const QVector<Media>& media);
    bool addClip(const QString& mediaId, Frame start);
    bool split(const QString& clipId, Frame at);
    bool remove(const QString& clipId, bool ripple = false);
    bool move(const QString& clipId, Frame start);
    bool trim(const QString& clipId, Frame start, Frame in, Frame length);
  public slots:
    void undo();
    void redo();
  signals:
    void changed();
    void savedStateChanged();
    void error(const QString& message);

  private:
    struct Entry {
        QString label;
        Project project;
    };
    Project project_, saved_;
    QVector<Entry> undo_, redo_;
};
} // namespace sentinel
