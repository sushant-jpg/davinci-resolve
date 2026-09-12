#include "TimelineWidget.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>
namespace sentinel {
TimelineWidget::TimelineWidget(Editor* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
    setAcceptDrops(true);
    setMinimumHeight(200);
    setMouseTracking(true);
    connect(editor_, &Editor::changed, this, [this] {
        // A command or project replacement invalidates the range captured at mouse-down.
        dragging_ = false;
        gesture_ = Gesture::None;
        bool exists = false;
        for (const auto& c : editor_->project().clips)
            if (c.id == selected_)
                exists = true;
        if (!exists) {
            selected_.clear();
            emit selectionChanged(selected_);
        }
        resize(sizeHint());
        updateGeometry();
        update();
    });
}
QSize TimelineWidget::sizeHint() const {
    return {int(std::min(1000000.0, 180 + std::max<Frame>(kFps * 30, editor_->project().duration() + 150) *
                                              pixelsPerFrame_)),
            200};
}
void TimelineWidget::setPlayhead(Frame f) {
    playhead_ = f;
    update();
}
void TimelineWidget::setZoom(int value) {
    pixelsPerFrame_ = std::pow(2.0, value / 20.0) / 4;
    resize(sizeHint());
    updateGeometry();
    update();
}
QRectF TimelineWidget::clipRect(const Clip& c) const {
    return {120 + c.start * pixelsPerFrame_, 64, std::max(2.0, c.length * pixelsPerFrame_), 104};
}
Frame TimelineWidget::frameAt(double x) const {
    return std::clamp<Frame>(std::llround((x - 120) / pixelsPerFrame_), 0, kMaxFrames);
}
Frame TimelineWidget::snap(Frame f, const QString& excluding) const {
    if (!snapping_)
        return f;
    Frame best = f;
    double distance = 9 / pixelsPerFrame_;
    auto consider = [&](Frame target) {
        if (std::abs(double(f - target)) < distance) {
            distance = std::abs(double(f - target));
            best = target;
        }
    };
    consider(0);
    consider(playhead_);
    for (const auto& c : editor_->project().clips)
        if (c.id != excluding) {
            consider(c.start);
            consider(c.end());
        }
    return best;
}
void TimelineWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.fillRect(rect(), QColor("#111820"));
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(QRect(0, 0, width(), 44), QColor("#1c2530"));
    p.setPen(QColor("#8a9cad"));
    const int interval = std::max(1, int(std::ceil(90 / (pixelsPerFrame_ * kFps))));
    const auto visible = visibleRegion().boundingRect();
    const Frame first = std::max<Frame>(0, frameAt(visible.left()) / (kFps * interval) * kFps * interval);
    for (Frame f = first; 120 + f * pixelsPerFrame_ < visible.right(); f += kFps * interval) {
        const double x = 120 + f * pixelsPerFrame_;
        p.drawLine(QPointF(x, 30), QPointF(x, height()));
        p.drawText(QPointF(x + 5, 22), timecode(f));
    }
    p.fillRect(QRect(0, 44, 116, height() - 44), QColor("#1c2530"));
    p.setPen(QColor("#dbe5ed"));
    p.drawText(QRect(14, 70, 100, 30), "V1 + A1");
    p.setPen(QColor("#8a9cad"));
    p.drawText(QRect(14, 106, 100, 40), "Linked\nmedia");
    if (editor_->project().clips.isEmpty())
        p.drawText(QRect(145, 78, 650, 80), Qt::AlignLeft | Qt::AlignVCenter,
                   "Drag media here to start editing\nDrop into an empty range • Edges trim • S splits at "
                   "the playhead");
    auto draw = [&](const Clip& c, bool ghost) {
        const auto r = clipRect(c);
        if (!r.intersects(visible))
            return;
        const auto* media = editor_->project().findMedia(c.mediaId);
        const bool selected = c.id == selected_;
        p.setBrush(QColor(ghost ? "#437c88" : selected ? "#316c7c" : "#294c63"));
        p.setPen(QPen(QColor(selected ? "#73e2cb" : "#426783"), selected ? 2 : 1));
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 5, 5);
        p.save();
        p.setClipRect(r.adjusted(7, 4, -7, -4));
        p.setPen(QColor("#f0f6fa"));
        p.drawText(r.adjusted(12, 10, -10, -10), Qt::AlignTop | Qt::AlignLeft,
                   media ? media->name : "Missing media");
        p.setPen(QColor("#b2d0de"));
        p.drawText(r.adjusted(12, 38, -10, -10), Qt::AlignTop | Qt::AlignLeft,
                   timecode(c.length) + "  •  IN " + timecode(c.in));
        p.drawText(r.adjusted(12, 70, -10, -2), Qt::AlignTop | Qt::AlignLeft,
                   media && media->audio ? "Audio linked" : "Video only");
        p.restore();
        if (selected) {
            p.setPen(QPen(QColor("#a5f1e0"), 2));
            p.drawLine(r.topLeft() + QPointF(5, 36), r.bottomLeft() + QPointF(5, -36));
            p.drawLine(r.topRight() + QPointF(-5, 36), r.bottomRight() + QPointF(-5, -36));
        }
    };
    for (const auto& c : editor_->project().clips)
        draw(c, false);
    if (dragging_) {
        p.setOpacity(0.7);
        draw(ghost_, true);
        p.setOpacity(1);
    }
    const double head = 120 + playhead_ * pixelsPerFrame_;
    p.setPen(QPen(QColor("#ffbe7b"), 2));
    p.drawLine(QPointF(head, 0), QPointF(head, height()));
    p.setBrush(QColor("#ffbe7b"));
    p.drawEllipse(QPointF(head, 8), 4, 4);
}
void TimelineWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    gesture_ = Gesture::None;
    dragging_ = false;
    pressFrame_ = frameAt(e->position().x());
    for (const auto& c : editor_->project().clips) {
        const auto r = clipRect(c);
        if (!r.contains(e->position()))
            continue;
        selected_ = c.id;
        original_ = ghost_ = c;
        gesture_ = e->position().x() - r.left() < 9    ? Gesture::TrimStart
                   : r.right() - e->position().x() < 9 ? Gesture::TrimEnd
                                                       : Gesture::Move;
        emit selectionChanged(selected_);
        update();
        return;
    }
    selected_.clear();
    emit selectionChanged(selected_);
    emit seekRequested(pressFrame_);
    update();
}
void TimelineWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!(e->buttons() & Qt::LeftButton) || gesture_ == Gesture::None)
        return;
    dragging_ = true;
    ghost_ = original_;
    Frame delta = frameAt(e->position().x()) - pressFrame_;
    if (gesture_ == Gesture::Move) {
        ghost_.start = snap(std::max<Frame>(0, original_.start + delta), original_.id);
        const Frame endSnap = snap(ghost_.end(), original_.id);
        if (endSnap != ghost_.end())
            ghost_.start = std::max<Frame>(0, endSnap - ghost_.length);
    } else if (gesture_ == Gesture::TrimStart) {
        delta = snap(original_.start + delta, original_.id) - original_.start;
        delta = std::clamp<Frame>(delta, -std::min(original_.in, original_.start), original_.length - 1);
        ghost_.start += delta;
        ghost_.in += delta;
        ghost_.length -= delta;
    } else {
        const auto* m = editor_->project().findMedia(original_.mediaId);
        const Frame end = snap(original_.end() + delta, original_.id);
        ghost_.length = std::clamp<Frame>(end - original_.start, 1, m->frames - original_.in);
    }
    update();
}
void TimelineWidget::mouseReleaseEvent(QMouseEvent*) {
    const bool commit = dragging_;
    dragging_ = false;
    gesture_ = Gesture::None;
    if (commit)
        editor_->trim(original_.id, ghost_.start, ghost_.in, ghost_.length);
    update();
}
void TimelineWidget::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasFormat("application/x-sentinel-media"))
        e->acceptProposedAction();
}
void TimelineWidget::dropEvent(QDropEvent* e) {
    const QString id = QString::fromUtf8(e->mimeData()->data("application/x-sentinel-media"));
    if (editor_->addClip(id, snap(frameAt(e->position().x()))))
        e->acceptProposedAction();
}
} // namespace sentinel
