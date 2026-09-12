#pragma once
#include "core/Editor.h"
#include <QFutureWatcher>
#include <QMainWindow>
#include <atomic>
#include <memory>
class QListWidget;
class QLineEdit;
class QLabel;
class QSpinBox;
class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;
class QSlider;
class QPushButton;
class QProgressDialog;
namespace sentinel {
class TimelineWidget;
class PlaybackController;
struct ImportResult {
    QVector<Media> media;
    QStringList errors;
};
class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    Editor* editor() { return &editor_; }
    PlaybackController* playback() { return playback_; }
    TimelineWidget* timeline() { return timeline_; }
    void openProject(const QString& path);

  protected:
    void closeEvent(QCloseEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

  private:
    void buildMenus();
    void buildWorkspace();
    void refresh();
    void inspectSelection();
    void importFiles(const QStringList& paths = {});
    void previewSelected();
    void relinkSelected();
    void exportProject();
    void newProject();
    bool saveProject(bool saveAs = false);
    bool maybeSave();
    void autosave();
    void recover();
    void remember(const QString& path);
    QString recoveryPath() const;
    void configureShortcuts();
    void showError(const QString& message);
    Editor editor_;
    TimelineWidget* timeline_ = nullptr;
    PlaybackController* playback_ = nullptr;
    QListWidget* bin_ = nullptr;
    QLineEdit* search_ = nullptr;
    QMediaPlayer* source_ = nullptr;
    QAudioOutput* sourceAudio_ = nullptr;
    QLabel *time_ = nullptr, *clipInfo_ = nullptr, *projectInfo_ = nullptr;
    QSpinBox *start_ = nullptr, *in_ = nullptr, *length_ = nullptr;
    QSlider* sourcePosition_ = nullptr;
    QPushButton* applyTrim_ = nullptr;
    QString projectPath_;
    int generation_ = 0;
    QFutureWatcher<ImportResult> importWatcher_;
    QFutureWatcher<QString> exportWatcher_;
    std::shared_ptr<std::atomic_bool> cancelExport_;
    std::shared_ptr<std::atomic_bool> cancelImport_;
    QProgressDialog* exportProgress_ = nullptr;
    QByteArray defaultLayout_;
};
} // namespace sentinel
