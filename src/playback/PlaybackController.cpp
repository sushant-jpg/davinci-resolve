#include "PlaybackController.h"
#include <QFileInfo>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>
#include <algorithm>
namespace sentinel {
PlaybackController::PlaybackController(Editor* editor, QVideoWidget* video, QObject* parent)
    : QObject(parent), editor_(editor) {
    player_.setAudioOutput(&audio_);
    audio_.setVolume(0.8);
    audio_.setMuted(true);
    player_.setVideoOutput(video);
    timer_.setInterval(15);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &PlaybackController::tick);
    connect(editor_, &Editor::changed, this, [this] {
        pause();
        seek(std::min(frame_, editor_->project().duration()));
    });
    connect(&player_, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::LoadedMedia) {
            if (!active_)
                return;
            player_.setPosition(pendingMs_);
            // Decode a first frame even when the timeline is paused. Audio stays muted.
            if (playing_ || (active_ && editor_->project().findMedia(active_->mediaId)->video))
                player_.play();
        } else if (status == QMediaPlayer::EndOfMedia && playing_ && active_) {
            frame_ = active_->end();
            mapFrame();
            emit positionChanged(frame_);
        }
    });
    connect(player_.videoSink(), &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame& frame) {
        if (!playing_ && frame.isValid())
            player_.pause();
    });
    connect(&player_, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& message) {
                pause();
                emit error(message);
            });
}
void PlaybackController::pause() {
    playing_ = false;
    audio_.setMuted(true);
    timer_.stop();
    player_.pause();
    emit playingChanged(false);
}
void PlaybackController::toggle() {
    if (playing_) {
        pause();
        return;
    }
    if (editor_->project().duration() == 0)
        return;
    if (frame_ >= editor_->project().duration())
        frame_ = 0;
    playing_ = true;
    audio_.setMuted(false);
    mapFrame();
    if (playing_) {
        timer_.start();
        emit playingChanged(true);
    }
}
void PlaybackController::seek(Frame frame) {
    frame_ = std::clamp<Frame>(frame, 0, editor_->project().duration());
    mapFrame();
    emit positionChanged(frame_);
}
void PlaybackController::step(int direction) {
    pause();
    seek(frame_ + direction);
}
void PlaybackController::mapFrame() {
    const auto* clip = editor_->project().clipAt(frame_);
    if (!clip) {
        active_.reset();
        player_.pause();
        emit videoVisible(false);
        gapStart_ = frame_;
        gapClock_.restart();
        if (frame_ >= editor_->project().duration())
            pause();
        return;
    }
    active_ = *clip;
    const auto* media = editor_->project().findMedia(clip->mediaId);
    if (!media || !QFileInfo::exists(media->path)) {
        pause();
        emit videoVisible(false);
        emit error("Missing media. Relink it in the media bin.");
        return;
    }
    emit videoVisible(media->video);
    pendingMs_ = ((clip->in + frame_ - clip->start) * 1000 + kFps / 2) / kFps;
    const auto url = QUrl::fromLocalFile(media->path);
    if (player_.source() != url) {
        player_.setSource(url);
    } else
        player_.setPosition(pendingMs_);
    if (playing_ && player_.mediaStatus() != QMediaPlayer::LoadingMedia)
        player_.play();
}
void PlaybackController::tick() {
    if (!playing_)
        return;
    if (active_) {
        // Do not map a previous source position while the next decoder is loading.
        if (player_.mediaStatus() == QMediaPlayer::LoadingMedia ||
            player_.playbackState() != QMediaPlayer::PlayingState)
            return;
        const Frame sourceFrame = (player_.position() * kFps + 500) / 1000;
        frame_ = std::max(active_->start, active_->start + sourceFrame - active_->in);
        if (frame_ >= active_->end()) {
            frame_ = active_->end();
            mapFrame();
        }
    } else {
        frame_ = gapStart_ + gapClock_.elapsed() * kFps / 1000;
        // Clamp at the next clip so delayed timers never skip short clips.
        for (const auto& c : editor_->project().clips)
            if (c.start >= gapStart_ && c.start <= frame_) {
                frame_ = c.start;
                break;
            }
        if (editor_->project().clipAt(frame_))
            mapFrame();
        if (frame_ >= editor_->project().duration()) {
            frame_ = editor_->project().duration();
            pause();
        }
    }
    emit positionChanged(frame_);
}
} // namespace sentinel
