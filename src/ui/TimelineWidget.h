#pragma once
#include "core/Editor.h"
#include <QWidget>
namespace sentinel {
class TimelineWidget : public QWidget {
    Q_OBJECT
  public:
    explicit TimelineWidget(Editor* editor, QWidget* parent = nullptr);
    QString selection() const { return selected_; }
    Frame playhead() const { return playhead_; }
    void setPlayhead(Frame frame);
    void setZoom(int value);
    void setSnapping(bool enabled) { snapping_ = enabled; }
    QSize sizeHint() const override;
  signals:
    void seekRequested(sentinel::Frame frame);
    void selectionChanged(const QString& id);

  protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

  private:
    QRectF clipRect(const Clip& clip) const;
    Frame frameAt(double x) const;
    Frame snap(Frame frame, const QString& excluding = {}) const;
    Editor* editor_;
    QString selected_;
    Frame playhead_ = 0, pressFrame_ = 0;
    double pixelsPerFrame_ = 2;
    bool snapping_ = true, dragging_ = false;
    enum class Gesture { None, Move, TrimStart, TrimEnd } gesture_ = Gesture::None;
    Clip original_, ghost_;
};
} // namespace sentinel
