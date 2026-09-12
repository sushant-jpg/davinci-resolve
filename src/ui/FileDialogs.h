#pragma once
#include <QFileDialog>
#include <QMessageBox>
namespace sentinel {
inline QString chooseSavePath(QWidget* parent, const QString& title, const QString& suggested,
                              const QString& filter, const QString& suffix) {
    QFileDialog dialog(parent, title);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilter(filter);
    // Resolve the extension before Qt asks whether to overwrite an existing file.
    dialog.setDefaultSuffix(suffix);
    dialog.selectFile(suggested);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return {};
    const auto path = dialog.selectedFiles().first();
    if (!path.endsWith("." + suffix, Qt::CaseInsensitive)) {
        QMessageBox::warning(parent, "Unsupported file extension", "Choose a ." + suffix + " filename.");
        return {};
    }
    return path;
}
} // namespace sentinel
