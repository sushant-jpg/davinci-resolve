#include "FileDialogs.h"
#include "MainWindow.h"
#include "export/Exporter.h"
#include "media/MediaProbe.h"
#include "playback/PlaybackController.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QListWidget>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStatusBar>
#include <QtConcurrent/QtConcurrentRun>
namespace sentinel {
void MainWindow::importFiles(const QStringList& paths) {
    if (importWatcher_.isRunning()) {
        statusBar()->showMessage("Media import is still running", 5000);
        return;
    }
    QStringList files = paths;
    if (files.isEmpty())
        files = QFileDialog::getOpenFileNames(
            this, "Import media", {},
            "Video and audio (*.mp4 *.mov *.mkv *.avi *.mxf *.webm *.wav *.mp3 *.aac *.flac *.m4a *.ogg)");
    if (files.isEmpty())
        return;
    if (files.size() > 100) {
        showError("Import up to 100 files at a time.");
        return;
    }
    cancelImport_ = std::make_shared<std::atomic_bool>(false);
    const auto cancel = cancelImport_;
    const int generation = generation_;
    statusBar()->showMessage("Reading media metadata in background…");
    disconnect(&importWatcher_, nullptr, this, nullptr);
    connect(&importWatcher_, &QFutureWatcher<ImportResult>::finished, this, [this, generation] {
        if (generation != generation_) {
            statusBar()->showMessage("Import discarded because the project changed", 5000);
            return;
        }
        const auto result = importWatcher_.result();
        editor_.importMedia(result.media);
        statusBar()->showMessage(
            QString("Imported %1 files; %2 failed").arg(result.media.size()).arg(result.errors.size()),
            10000);
        if (!result.errors.isEmpty())
            showError(result.errors.join("\n\n"));
    });
    importWatcher_.setFuture(QtConcurrent::run([files, cancel] {
        ImportResult result;
        for (const auto& path : files) {
            if (cancel->load())
                break;
            try {
                result.media.push_back(MediaProbe::probe(path, cancel.get()));
            } catch (const std::exception& e) {
                result.errors << QFileInfo(path).fileName() + ": " + QString::fromUtf8(e.what());
            }
        }
        return result;
    }));
}
void MainWindow::previewSelected() {
    if (!bin_->currentItem())
        return;
    const auto* media = editor_.project().findMedia(bin_->currentItem()->data(Qt::UserRole).toString());
    if (!media)
        return;
    if (!QFileInfo::exists(media->path)) {
        showError("Media is offline. Use Relink selected media.");
        return;
    }
    playback_->pause();
    source_->setSource(QUrl::fromLocalFile(media->path));
    source_->play();
}
void MainWindow::relinkSelected() {
    if (importWatcher_.isRunning()) {
        showError("Wait for the current import to finish.");
        return;
    }
    if (!bin_->currentItem())
        return;
    const auto id = bin_->currentItem()->data(Qt::UserRole).toString();
    const auto path = QFileDialog::getOpenFileName(this, "Relink selected media");
    if (path.isEmpty())
        return;
    cancelImport_ = std::make_shared<std::atomic_bool>(false);
    const auto cancel = cancelImport_;
    const int generation = generation_;
    disconnect(&importWatcher_, nullptr, this, nullptr);
    connect(&importWatcher_, &QFutureWatcher<ImportResult>::finished, this, [this, id, generation] {
        if (generation != generation_)
            return;
        const auto result = importWatcher_.result();
        if (!result.errors.isEmpty()) {
            showError(result.errors.join("\n"));
            return;
        }
        editor_.command("Relink media", [&](Project& p) {
            for (auto& m : p.media)
                if (m.id == id) {
                    m = result.media.first();
                    m.id = id;
                    return;
                }
            throw std::runtime_error("Media was removed while relinking");
        });
    });
    statusBar()->showMessage("Probing replacement media…");
    importWatcher_.setFuture(QtConcurrent::run([path, cancel] {
        ImportResult r;
        try {
            r.media.push_back(MediaProbe::probe(path, cancel.get()));
        } catch (const std::exception& e) {
            r.errors << QString::fromUtf8(e.what());
        }
        return r;
    }));
}
void MainWindow::exportProject() {
    if (exportWatcher_.isRunning()) {
        showError("An export is already running.");
        return;
    }
    if (editor_.project().clips.isEmpty()) {
        showError("Add clips to the timeline before exporting.");
        return;
    }
    auto path = chooseSavePath(this, "Export H.264 / AAC • 1280 × 720 • 30 fps",
                               editor_.project().name + ".mp4", "MP4 video (*.mp4)", "mp4");
    if (path.isEmpty())
        return;

    if (QFileInfo(path).absoluteFilePath() == QFileInfo(projectPath_).absoluteFilePath()) {
        showError("Export cannot overwrite the project file.");
        return;
    }
    const Project snapshot = editor_.project();
    cancelExport_ = std::make_shared<std::atomic_bool>(false);
    const auto cancel = cancelExport_;
    exportProgress_ = new QProgressDialog("Preparing export…", "Cancel", 0, 100, this);
    exportProgress_->setWindowTitle("Render sequence");
    exportProgress_->setWindowModality(Qt::NonModal);
    exportProgress_->setMinimumDuration(0);
    exportProgress_->setAutoClose(false);
    connect(exportProgress_, &QProgressDialog::canceled, this, [cancel] { cancel->store(true); });
    disconnect(&exportWatcher_, nullptr, this, nullptr);
    connect(&exportWatcher_, &QFutureWatcher<QString>::finished, this, [this, path] {
        const auto error = exportWatcher_.result();
        exportProgress_->close();
        exportProgress_->deleteLater();
        exportProgress_ = nullptr;
        if (error.isEmpty())
            statusBar()->showMessage("Export complete: " + path, 20000);
        else if (error == "Export canceled")
            statusBar()->showMessage(error, 5000);
        else
            showError(error);
    });
    exportWatcher_.setFuture(QtConcurrent::run([this, snapshot, path, cancel] {
        try {
            Exporter::render(snapshot, path, *cancel, [this](int value, const QString& text) {
                QMetaObject::invokeMethod(
                    this,
                    [this, value, text] {
                        if (exportProgress_) {
                            exportProgress_->setValue(value);
                            exportProgress_->setLabelText(text);
                        }
                    },
                    Qt::QueuedConnection);
            });
            return QString();
        } catch (const std::exception& e) {
            return QString::fromUtf8(e.what());
        }
    }));
}
} // namespace sentinel
