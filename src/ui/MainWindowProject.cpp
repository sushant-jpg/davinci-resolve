#include "FileDialogs.h"
#include "MainWindow.h"
#include "playback/PlaybackController.h"
#include "project/ProjectIO.h"
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <stdexcept>
namespace sentinel {
QString MainWindow::recoveryPath() const {
    const auto folder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(folder);
    return QDir(folder).filePath("recovery.sentinel");
}
void MainWindow::remember(const QString& path) {
    QSettings settings;
    auto recent = settings.value("recent").toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 10)
        recent.removeLast();
    settings.setValue("recent", recent);
}
bool MainWindow::maybeSave() {
    if (!editor_.dirty())
        return true;
    const auto choice = QMessageBox::question(
        this, "Unsaved project", "Save changes to " + editor_.project().name + "?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Cancel)
        return false;
    return choice != QMessageBox::Save || saveProject();
}
void MainWindow::newProject() {
    bool ok = false;
    const auto name =
        QInputDialog::getText(this, "New project", "Project name", QLineEdit::Normal, "Untitled", &ok)
            .trimmed();
    if (!ok || name.isEmpty() || !maybeSave())
        return;
    try {
        Project p;
        p.name = name;
        p.validate();
        ++generation_;
        if (cancelImport_)
            cancelImport_->store(true);
        source_->stop();
        source_->setSource({});
        playback_->pause();
        projectPath_.clear();
        editor_.reset(p);
        playback_->seek(0);
    } catch (const std::exception& e) {
        showError(e.what());
    }
}
void MainWindow::openProject(const QString& path) {
    try {
        // Parse and validate first; failed loads preserve the current document.
        Project p = ProjectIO::load(path);
        if (!maybeSave())
            return;
        // Saving from the prompt may have replaced the file we are opening.
        p = ProjectIO::load(path);
        ++generation_;
        if (cancelImport_)
            cancelImport_->store(true);
        source_->stop();
        source_->setSource({});
        playback_->pause();
        projectPath_ = QFileInfo(path).absoluteFilePath();
        editor_.reset(std::move(p));
        playback_->seek(0);
        remember(projectPath_);
        int missing = 0;
        for (const auto& m : editor_.project().media)
            if (!QFileInfo::exists(m.path))
                ++missing;
        statusBar()->showMessage(
            missing
                ? QString("Loaded with %1 missing media files — select and relink in the bin.").arg(missing)
                : "Project loaded",
            15000);
    } catch (const std::exception& e) {
        showError(e.what());
    }
}
bool MainWindow::saveProject(bool saveAs) {
    QString path = projectPath_;
    if (path.isEmpty() || saveAs) {
        path =
            chooseSavePath(this, "Save project", path.isEmpty() ? editor_.project().name + ".sentinel" : path,
                           "Sentinel project (*.sentinel)", "sentinel");
        if (path.isEmpty())
            return false;
    }
    try {
        ProjectIO::save(editor_.project(), path);
        projectPath_ = QFileInfo(path).absoluteFilePath();
        editor_.markSaved();
        remember(projectPath_);
        statusBar()->showMessage("Project saved atomically; previous version is in .bak", 5000);
        return true;
    } catch (const std::exception& e) {
        showError(e.what());
        return false;
    }
}
void MainWindow::autosave() {
    if (!editor_.dirty())
        return;
    try {
        ProjectIO::save(editor_.project(), recoveryPath());
        statusBar()->showMessage("Recovery snapshot saved", 2000);
    } catch (const std::exception& e) {
        statusBar()->showMessage("Autosave failed: " + QString::fromUtf8(e.what()), 10000);
    }
}
void MainWindow::recover() {
    const auto path = recoveryPath();
    if (!QFileInfo::exists(path)) {
        QMessageBox::information(
            this, "Recovery", "No autosave is available yet. Unsaved changes are captured every 30 seconds.");
        return;
    }
    try {
        Project p = ProjectIO::load(path);
        if (!maybeSave())
            return;
        ++generation_;
        if (cancelImport_)
            cancelImport_->store(true);
        source_->stop();
        source_->setSource({});
        playback_->pause();
        projectPath_.clear();
        editor_.reset();
        editor_.command("Recover project", [&p](Project& target) { target = p; });
        playback_->seek(0);
        statusBar()->showMessage("Recovered snapshot. Use Save to choose a project file.", 10000);
    } catch (const std::exception& e) {
        showError(e.what());
    }
}
} // namespace sentinel
