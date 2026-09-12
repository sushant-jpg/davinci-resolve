#include "MainWindow.h"
#include "TimelineWidget.h"
#include "playback/PlaybackController.h"
#include <QAction>
#include <QAudioOutput>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMediaPlayer>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVideoWidget>
namespace sentinel {
namespace {
class MediaBin final : public QListWidget {
  public:
    using QListWidget::QListWidget;
    void startDrag(Qt::DropActions) override {
        if (!currentItem())
            return;
        auto* mime = new QMimeData;
        mime->setData("application/x-sentinel-media", currentItem()->data(Qt::UserRole).toString().toUtf8());
        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
        drag->deleteLater();
    }
};
QWidget* column(std::initializer_list<QWidget*> widgets) {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    for (auto* widget : widgets) {
        if (qobject_cast<QLabel*>(widget))
            widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        layout->addWidget(widget);
    }
    return panel;
}
} // namespace
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setObjectName("SentinelStudio");
    setAcceptDrops(true);
    resize(1440, 900);
    setMinimumSize(960, 640);
    setDockOptions(AllowNestedDocks | AllowTabbedDocks | AnimatedDocks);
    buildWorkspace();
    buildMenus();
    connect(&editor_, &Editor::changed, this, &MainWindow::refresh);
    connect(&editor_, &Editor::savedStateChanged, this, &MainWindow::refresh);
    connect(&editor_, &Editor::error, this, &MainWindow::showError);
    connect(timeline_, &TimelineWidget::selectionChanged, this, &MainWindow::inspectSelection);
    defaultLayout_ = saveState();
    QSettings settings;
    if (settings.contains("window/geometry"))
        restoreGeometry(settings.value("window/geometry").toByteArray());
    if (settings.contains("window/layout"))
        restoreState(settings.value("window/layout").toByteArray());
    auto* timer = new QTimer(this);
    timer->setInterval(30000);
    connect(timer, &QTimer::timeout, this, &MainWindow::autosave);
    timer->start();
    refresh();
}
MainWindow::~MainWindow() {
    if (cancelExport_)
        cancelExport_->store(true);
    if (cancelImport_)
        cancelImport_->store(true);
    exportWatcher_.waitForFinished();
    importWatcher_.waitForFinished();
}
void MainWindow::buildWorkspace() {
    auto* toolbar = addToolBar("Editing");
    toolbar->setObjectName("editingToolbar");
    toolbar->setMovable(false);
    auto* brand = new QLabel("  SENTINEL  /  STUDIO     ");
    brand->setStyleSheet("color:#7ee6cb; font-weight:700; letter-spacing:2px");
    toolbar->addWidget(brand);
    auto* import = toolbar->addAction("Import media");
    connect(import, &QAction::triggered, this, [this] { importFiles(); });
    auto* append = toolbar->addAction("Append to timeline");
    connect(append, &QAction::triggered, this, [this] {
        if (bin_->currentItem())
            editor_.addClip(bin_->currentItem()->data(Qt::UserRole).toString(), editor_.project().duration());
    });
    toolbar->addSeparator();
    auto* exportAction = toolbar->addAction("Export MP4");
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportProject);
    auto* flexible = new QWidget;
    flexible->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(flexible);
    projectInfo_ = new QLabel;
    toolbar->addWidget(projectInfo_);
    auto dock = [this](const QString& title, const QString& id, QWidget* content, Qt::DockWidgetArea area) {
        auto* d = new QDockWidget(title, this);
        d->setObjectName(id);
        d->setWidget(content);
        addDockWidget(area, d);
        return d;
    };
    search_ = new QLineEdit;
    search_->setPlaceholderText("Search media…");
    search_->setClearButtonEnabled(true);
    bin_ = new MediaBin;
    bin_->setObjectName("mediaBin");
    bin_->setDragEnabled(true);
    bin_->setSpacing(4);
    bin_->setMinimumWidth(220);
    auto* hint = new QLabel("Double-click to preview.\nDrag media to the timeline.");
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#8b9dab");
    auto* relink = new QPushButton("Relink selected media");
    connect(relink, &QPushButton::clicked, this, &MainWindow::relinkSelected);
    dock("MEDIA LIBRARY", "mediaDock", column({search_, bin_, hint, relink}), Qt::LeftDockWidgetArea);
    connect(search_, &QLineEdit::textChanged, this, [this](const QString& text) {
        for (int i = 0; i < bin_->count(); ++i)
            bin_->item(i)->setHidden(!bin_->item(i)->text().contains(text, Qt::CaseInsensitive));
    });
    connect(bin_, &QListWidget::itemDoubleClicked, this, [this] { previewSelected(); });
    auto* sourceVideo = new QVideoWidget;
    sourceVideo->setMinimumSize(220, 100);
    source_ = new QMediaPlayer(this);
    source_->setObjectName("sourcePlayer");
    sourceAudio_ = new QAudioOutput(this);
    source_->setAudioOutput(sourceAudio_);
    source_->setVideoOutput(sourceVideo);
    sourcePosition_ = new QSlider(Qt::Horizontal);
    sourcePosition_->setRange(0, 0);
    auto* sourcePlay = new QPushButton("Play / pause source");
    connect(sourcePlay, &QPushButton::clicked, this, [this] {
        playback_->pause();
        if (source_->playbackState() == QMediaPlayer::PlayingState)
            source_->pause();
        else
            source_->play();
    });
    connect(source_, &QMediaPlayer::durationChanged, this,
            [this](qint64 ms) { sourcePosition_->setRange(0, int(ms)); });
    connect(source_, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
        if (!sourcePosition_->isSliderDown())
            sourcePosition_->setValue(int(ms));
    });
    connect(sourcePosition_, &QSlider::sliderMoved, source_, &QMediaPlayer::setPosition);
    connect(source_, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& message) { showError("Source preview: " + message); });
    dock("SOURCE VIEWER", "sourceDock", column({sourceVideo, sourcePosition_, sourcePlay}),
         Qt::LeftDockWidgetArea);
    auto* programVideo = new QVideoWidget;
    programVideo->setMinimumSize(320, 200);
    auto* viewer = new QWidget;
    viewer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewer->setStyleSheet("background:#080d12");
    auto* viewLayout = new QVBoxLayout(viewer);
    viewLayout->addWidget(programVideo);
    auto* empty = new QLabel("SENTINEL STUDIO\n\nImport media and build your first sequence");
    empty->setAlignment(Qt::AlignCenter);
    empty->setMinimumHeight(200);
    viewLayout->addWidget(empty);
    playback_ = new PlaybackController(&editor_, programVideo, this);
    programVideo->hide();
    connect(playback_, &PlaybackController::videoVisible, this, [programVideo, empty](bool visible) {
        programVideo->setVisible(visible);
        empty->setVisible(!visible);
        empty->setText("SENTINEL STUDIO\n\nAudio / empty timeline range");
    });
    connect(playback_, &PlaybackController::error, this, &MainWindow::showError);
    auto* transport = new QWidget;
    auto* transportLayout = new QHBoxLayout(transport);
    auto* back = new QPushButton("−1 frame");
    auto* play = new QPushButton("Play");
    auto* forward = new QPushButton("+1 frame");
    time_ = new QLabel;
    transportLayout->addStretch();
    transportLayout->addWidget(back);
    transportLayout->addWidget(play);
    transportLayout->addWidget(forward);
    transportLayout->addStretch();
    transportLayout->addWidget(time_);
    connect(back, &QPushButton::clicked, this, [this] { playback_->step(-1); });
    connect(forward, &QPushButton::clicked, this, [this] { playback_->step(1); });
    connect(play, &QPushButton::clicked, playback_, &PlaybackController::toggle);
    connect(playback_, &PlaybackController::playingChanged, this, [this, play](bool active) {
        play->setText(active ? "Pause" : "Play");
        if (active)
            source_->pause();
    });
    auto* programLabel = new QLabel("PROGRAM  /  MAIN SEQUENCE");
    programLabel->setStyleSheet("color:#8fa5b5;letter-spacing:2px");
    setCentralWidget(column({programLabel, viewer, transport}));
    timeline_ = new TimelineWidget(&editor_);
    timeline_->setObjectName("timeline");
    auto* scroll = new QScrollArea;
    scroll->setWidget(timeline_);
    scroll->setWidgetResizable(false);
    scroll->setMinimumHeight(200);
    connect(timeline_, &TimelineWidget::seekRequested, playback_, &PlaybackController::seek);
    connect(playback_, &PlaybackController::positionChanged, this, [this](Frame frame) {
        timeline_->setPlayhead(frame);
        time_->setText(timecode(frame));
    });
    auto* timelineTools = new QWidget;
    auto* tools = new QHBoxLayout(timelineTools);
    auto* split = new QPushButton("Split at playhead");
    auto* remove = new QPushButton("Delete clip");
    auto* snap = new QPushButton("Snap");
    snap->setCheckable(true);
    snap->setChecked(true);
    auto* zoom = new QSlider(Qt::Horizontal);
    zoom->setRange(0, 100);
    zoom->setValue(60);
    zoom->setMaximumWidth(180);
    tools->addWidget(split);
    tools->addWidget(remove);
    tools->addWidget(snap);
    tools->addStretch();
    tools->addWidget(new QLabel("Timeline zoom"));
    tools->addWidget(zoom);
    connect(split, &QPushButton::clicked, this,
            [this] { editor_.split(timeline_->selection(), playback_->frame()); });
    connect(remove, &QPushButton::clicked, this, [this] { editor_.remove(timeline_->selection()); });
    connect(snap, &QPushButton::toggled, timeline_, &TimelineWidget::setSnapping);
    connect(zoom, &QSlider::valueChanged, timeline_, &TimelineWidget::setZoom);
    dock("TIMELINE  /  30 FPS", "timelineDock", column({timelineTools, scroll}), Qt::BottomDockWidgetArea);
    clipInfo_ = new QLabel("Select a timeline clip");
    clipInfo_->setWordWrap(true);
    clipInfo_->setTextFormat(Qt::PlainText);
    auto* formWidget = new QWidget;
    formWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* form = new QFormLayout(formWidget);
    start_ = new QSpinBox;
    in_ = new QSpinBox;
    length_ = new QSpinBox;
    for (auto* spin : {start_, in_, length_}) {
        spin->setRange(0, int(kMaxFrames));
        spin->setSuffix(" fr");
    }
    length_->setMinimum(1);
    form->addRow("Timeline start", start_);
    form->addRow("Source in", in_);
    form->addRow("Duration", length_);
    applyTrim_ = new QPushButton("Apply clip range");
    connect(applyTrim_, &QPushButton::clicked, this, [this] {
        editor_.trim(timeline_->selection(), start_->value(), in_->value(), length_->value());
    });
    auto* scope = new QLabel("PHASE 1\nOne linked video/audio track\n1280 × 720 • 30 fps\n\nComing "
                             "later\nMultitrack • Effects • Color\nCompositing • AI • Audio mixer");
    scope->setStyleSheet("color:#859aab;line-height:1.5");
    dock("CLIP INSPECTOR", "inspectorDock", column({clipInfo_, formWidget, applyTrim_, scope}),
         Qt::RightDockWidgetArea);
    resizeDocks({findChild<QDockWidget*>("timelineDock")}, {300}, Qt::Vertical);
}
void MainWindow::buildMenus() {
    auto* file = menuBar()->addMenu("Project");
    auto* edit = menuBar()->addMenu("Edit");
    auto* view = menuBar()->addMenu("View");
    auto action = [this](QMenu* menu, const QString& name, const QString& id, const QKeySequence& key,
                         auto fn) {
        auto* a = menu->addAction(name);
        a->setObjectName("shortcut/" + id);
        a->setShortcut(QSettings().value(a->objectName(), key.toString()).toString());
        connect(a, &QAction::triggered, this, fn);
        return a;
    };
    action(file, "New project…", "new", QKeySequence::New, [this] { newProject(); });
    action(file, "Open project…", "open", QKeySequence::Open, [this] {
        const auto p =
            QFileDialog::getOpenFileName(this, "Open project", {}, "Sentinel project (*.sentinel)");
        if (!p.isEmpty())
            openProject(p);
    });
    auto* recent = file->addMenu("Recent projects");
    connect(recent, &QMenu::aboutToShow, this, [this, recent] {
        recent->clear();
        for (const auto& path : QSettings().value("recent").toStringList()) {
            auto* a = recent->addAction(path);
            connect(a, &QAction::triggered, this, [this, path] { openProject(path); });
        }
        if (recent->isEmpty())
            recent->addAction("No recent projects")->setEnabled(false);
    });
    action(file, "Save", "save", QKeySequence::Save, [this] { saveProject(); });
    action(file, "Save as…", "saveAs", QKeySequence::SaveAs, [this] { saveProject(true); });
    action(file, "Import media…", "import", QKeySequence("Ctrl+I"), [this] { importFiles(); });
    action(file, "Export MP4…", "export", QKeySequence("Ctrl+M"), [this] { exportProject(); });
    file->addAction("Recover autosave…", this, &MainWindow::recover);
    file->addSeparator();
    file->addAction("Quit", this, &QWidget::close);
    auto* undo = action(edit, "Undo", "undo", QKeySequence::Undo, [this] { editor_.undo(); });
    auto* redo = action(edit, "Redo", "redo", QKeySequence::Redo, [this] { editor_.redo(); });
    connect(&editor_, &Editor::changed, this, [this, undo, redo] {
        undo->setEnabled(editor_.canUndo());
        redo->setEnabled(editor_.canRedo());
    });
    undo->setEnabled(false);
    redo->setEnabled(false);
    action(edit, "Split clip", "split", QKeySequence("S"),
           [this] { editor_.split(timeline_->selection(), playback_->frame()); });
    action(edit, "Delete clip", "delete", QKeySequence::Delete,
           [this] { editor_.remove(timeline_->selection()); });
    action(edit, "Ripple delete", "ripple", QKeySequence("Shift+Delete"),
           [this] { editor_.remove(timeline_->selection(), true); });
    action(edit, "Play / pause", "play", QKeySequence(Qt::Key_Space), [this] { playback_->toggle(); });
    action(edit, "Previous frame", "previous", QKeySequence(Qt::Key_Left), [this] { playback_->step(-1); });
    action(edit, "Next frame", "next", QKeySequence(Qt::Key_Right), [this] { playback_->step(1); });
    action(edit, "Go to start", "home", QKeySequence(Qt::Key_Home), [this] { playback_->seek(0); });
    edit->addAction("Keyboard shortcuts…", this, &MainWindow::configureShortcuts);
    for (auto* d : findChildren<QDockWidget*>())
        view->addAction(d->toggleViewAction());
    view->addAction("Reset layout", this, [this] { restoreState(defaultLayout_); });
    auto* help = menuBar()->addMenu("Help");
    help->addAction("About / roadmap", this, [this] {
        QMessageBox::information(
            this, "Sentinel Studio 0.1",
            "Original C++ / Qt editor — Phase 1 MVP\n\nDrag from the media bin to an empty timeline range. "
            "Drag clip edges to trim or edit exact frame values in the inspector. Select a clip and press S "
            "to split at the playhead.\n\nComing later: multitrack, GPU engine, color, VFX, professional "
            "audio, AI, plugins and collaboration.");
    });
}
void MainWindow::refresh() {
    setWindowTitle(editor_.project().name + (editor_.dirty() ? " *" : "") + " — Sentinel Studio");
    projectInfo_->setText("  720p / 30 fps  ·  " + timecode(editor_.project().duration()) + "   ");
    const QString selected =
        bin_->currentItem() ? bin_->currentItem()->data(Qt::UserRole).toString() : QString();
    bin_->clear();
    for (const auto& m : editor_.project().media) {
        auto* item =
            new QListWidgetItem(m.name + "\n" +
                                    (QFileInfo::exists(m.path) ? m.codec + "  ·  " + timecode(m.frames)
                                                               : "OFFLINE — relink media"),
                                bin_);
        item->setData(Qt::UserRole, m.id);
        item->setToolTip(m.path);
        item->setSizeHint(QSize(220, 56));
        item->setHidden(!item->text().contains(search_->text(), Qt::CaseInsensitive));
        if (m.id == selected)
            bin_->setCurrentItem(item);
    }
    if (!bin_->currentItem() && bin_->count())
        bin_->setCurrentRow(0);
    time_->setText(timecode(playback_->frame()));
    inspectSelection();
}
void MainWindow::inspectSelection() {
    const Clip* selected = nullptr;
    for (const auto& c : editor_.project().clips)
        if (c.id == timeline_->selection())
            selected = &c;
    for (auto* spin : {start_, in_, length_})
        spin->setEnabled(selected);
    applyTrim_->setEnabled(selected);
    if (!selected) {
        clipInfo_->setText("Select a timeline clip");
        return;
    }
    const auto* m = editor_.project().findMedia(selected->mediaId);
    clipInfo_->setText(m->name + "\n" + QString::number(m->width) + " × " + QString::number(m->height) +
                       " • " + m->codec);
    start_->setValue(int(selected->start));
    in_->setValue(int(selected->in));
    length_->setValue(int(selected->length));
}
void MainWindow::showError(const QString& message) {
    statusBar()->showMessage(message, 15000);
    QMessageBox box(QMessageBox::Warning, "Sentinel Studio", message, QMessageBox::Ok, this);
    box.setTextFormat(Qt::PlainText);
    box.exec();
}
void MainWindow::configureShortcuts() {
    QDialog dialog(this);
    dialog.setWindowTitle("Keyboard shortcuts");
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    QVector<QPair<QAction*, QKeySequenceEdit*>> fields;
    for (auto* a : findChildren<QAction*>())
        if (a->objectName().startsWith("shortcut/")) {
            auto* field = new QKeySequenceEdit(a->shortcut());
            form->addRow(a->text(), field);
            fields.push_back({a, field});
        }
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        QSet<QString> used;
        for (const auto& pair : fields) {
            const auto key = pair.second->keySequence().toString();
            if (!key.isEmpty() && used.contains(key)) {
                QMessageBox::warning(&dialog, "Duplicate shortcut", "Each shortcut must be unique.");
                return;
            }
            used.insert(key);
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted)
        return;
    QSettings settings;
    for (const auto& pair : fields) {
        pair.first->setShortcut(pair.second->keySequence());
        settings.setValue(pair.first->objectName(), pair.first->shortcut().toString());
    }
}
void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent* e) {
    QStringList files;
    for (const auto& u : e->mimeData()->urls())
        if (u.isLocalFile())
            files << u.toLocalFile();
    if (!files.isEmpty()) {
        importFiles(files);
        e->acceptProposedAction();
    } else {
        e->ignore();
    }
}
void MainWindow::closeEvent(QCloseEvent* e) {
    if (exportWatcher_.isRunning()) {
        showError("Cancel or finish the current export before closing.");
        e->ignore();
        return;
    }
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/layout", saveState());
    e->accept();
}
} // namespace sentinel
