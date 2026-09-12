#pragma once
#include "core/Editor.h"
#include <QAudioOutput>
#include <QElapsedTimer>
#include <QMediaPlayer>
#include <QTimer>
class QVideoWidget;
namespace sentinel {
class PlaybackController : public QObject {
    Q_OBJECT
  public:
    PlaybackController(Editor* editor, QVideoWidget* video, QObject* parent = nullptr);
    Frame frame() const { return frame_; }
    bool playing() const { return playing_; }
    QMediaPlayer* player() { return &player_; }
  public slots:
    void seek(Frame frame);
    void toggle();
    void pause();
    void step(int direction);
  signals:
    void positionChanged(sentinel::Frame frame);
    void playingChanged(bool playing);
    void videoVisible(bool visible);
    void error(const QString& message);

  private:
    void tick();
    void mapFrame();
    Editor* editor_;
    QMediaPlayer player_;
    QAudioOutput audio_;
    QTimer timer_;
    QElapsedTimer gapClock_;
    Frame frame_ = 0, gapStart_ = 0;
    std::optional<Clip> active_;
    bool playing_ = false;
    qint64 pendingMs_ = 0;
};
} // namespace sentinel
