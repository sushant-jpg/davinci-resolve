#include "ui/MainWindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QScreen>
#include <QStyleFactory>
#include <QTimer>
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("SentinelStudio");
    QCoreApplication::setApplicationName("Sentinel Studio");
    QCoreApplication::setApplicationVersion("0.1.0");
    app.setStyle(QStyleFactory::create("Fusion"));
    app.setStyleSheet(R"(
      QMainWindow, QDialog, QWidget { background:#151d26; color:#dbe5ed; font-family:Sans; font-size:12px; }
      QMenuBar, QToolBar { background:#1e2935; border-bottom:1px solid #30404e; padding:6px; }
      QMenuBar::item:selected, QMenu::item:selected { background:#315b69; }
      QDockWidget::title { background:#202c38; padding:9px; color:#96b0c2; font-weight:bold; }
      QMainWindow::separator { background:#0b1118; width:5px; height:5px; }
      QPushButton, QToolButton { background:#2a3b4a; border:1px solid #405567; border-radius:4px; padding:7px 12px; }
      QPushButton:hover, QToolButton:hover { background:#365568; border-color:#73d8c0; }
      QPushButton:checked { background:#245e5a; border-color:#73d8c0; }
      QPushButton:disabled { color:#71808d; background:#1e2a34; }
      QLineEdit, QSpinBox, QKeySequenceEdit { background:#101820; border:1px solid #354958; padding:7px; border-radius:4px; selection-background-color:#326c78; }
      QListWidget { background:#101820; border:0; padding:3px; }
      QListWidget::item { background:#1c2b37; border-radius:4px; padding:5px; }
      QListWidget::item:selected { background:#2b5365; color:#b7f2e4; }
      QScrollArea { border:0; } QStatusBar { background:#101820; color:#8fa7b8; }
      QSlider::groove:horizontal { height:4px; background:#3a4b5b; }
      QSlider::handle:horizontal { background:#7bdcc3; width:12px; margin:-4px 0; border-radius:5px; }
      QToolTip { background:#283b4b; color:#edf5fa; border:1px solid #527085; }
    )");
    QCommandLineParser parser;
    parser.setApplicationDescription("Sentinel Studio — Phase 1 desktop video editor");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("project", "Optional .sentinel project to open");
    QCommandLineOption smoke("smoke-test", "Open the application and exit after 2 seconds");
    parser.addOption(smoke);
    QCommandLineOption screenshot("screenshot", "Save a desktop verification screenshot and exit", "path");
    parser.addOption(screenshot);
    parser.process(app);
    sentinel::MainWindow window;
    window.show();
    if (!parser.positionalArguments().isEmpty())
        window.openProject(parser.positionalArguments().first());
    if (parser.isSet(screenshot))
        QTimer::singleShot(1500, &app, [&] {
            const auto shot = QGuiApplication::platformName() == "offscreen"
                                  ? window.grab()
                                  : window.screen()->grabWindow(window.winId());
            const bool ok = shot.save(parser.value(screenshot));
            app.exit(ok ? 0 : 1);
        });
    if (parser.isSet(smoke))
        QTimer::singleShot(2000, &app, &QCoreApplication::quit);
    return app.exec();
}
