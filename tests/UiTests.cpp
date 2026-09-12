#include "media/MediaProbe.h"
#include "playback/PlaybackController.h"
#include "project/ProjectIO.h"
#include "ui/MainWindow.h"
#include "ui/TimelineWidget.h"
#include <QAbstractButton>
#include <QApplication>
#include <QDockWidget>
#include <QDropEvent>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtTest>
using namespace sentinel;
class UiTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("SentinelTests");
        QCoreApplication::setApplicationName("SentinelTests");
    }
    void projectChangeCancelsTrimGesture() {
        Editor editor;
        Project p;
        p.media = {{"media", QDir::temp().filePath("test.mp4"), "test", 90, true, false, 320, 180, "h264"}};
        p.clips = {{"clip", "media", 0, 0, 90}};
        editor.reset(p);
        TimelineWidget timeline(&editor);
        timeline.resize(600, 240);
        timeline.show();
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(297, 100));
        editor.reset();
        QMouseEvent move(QEvent::MouseMove, QPointF(280, 100), timeline.mapToGlobal(QPoint(280, 100)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&timeline, &move);
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(280, 100));
        QVERIFY(editor.project().clips.isEmpty());
        QVERIFY(!editor.dirty());
    }
    void desktopAndPlayback() {
        QTemporaryDir dir;
        const auto path = dir.filePath("test.mp4");
        QProcess process;
        process.start(MediaProbe::executable("ffmpeg"),
                      {"-v", "error", "-f", "lavfi", "-i", "testsrc2=s=320x180:r=30:d=3", "-f", "lavfi", "-i",
                       "sine=frequency=440:duration=3", "-c:v", "libx264", "-threads", "1", "-c:a", "aac",
                       "-shortest", path});
        QVERIFY(process.waitForFinished(30000));
        QCOMPARE(process.exitCode(), 0);
        MainWindow window;
        window.show();
        QTest::qWait(100);
        QCOMPARE(window.findChildren<QDockWidget*>().size(), 4);
        const auto media = MediaProbe::probe(path);
        QVERIFY(window.editor()->importMedia({media}));
        // Exercise the timeline's actual MIME drop route.
        QMimeData mime;
        mime.setData("application/x-sentinel-media", media.id.toUtf8());
        QDragEnterEvent enter(QPoint(120, 90), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(window.timeline(), &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(120, 90), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(window.timeline(), &drop);
        QVERIFY(drop.isAccepted());
        QCOMPARE(window.editor()->project().clips.size(), 1);
        QSignalSpy frames(window.playback()->player()->videoSink(), &QVideoSink::videoFrameChanged);
        QTRY_VERIFY_WITH_TIMEOUT(window.playback()->player()->videoSink()->videoFrame().isValid(), 10000);
        QVERIFY(!window.playback()->playing());
        window.playback()->toggle();
        QTRY_VERIFY_WITH_TIMEOUT(window.playback()->frame() > 5, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() > 0, 10000);
        window.editor()->markSaved();
        QVERIFY2(window.playback()->playing(), "Saving must not stop playback");
        window.playback()->pause();
        const auto at = window.playback()->frame();
        window.playback()->step(1);
        QCOMPARE(window.playback()->frame(), at + 1);
        window.playback()->seek(30);
        QCOMPARE(window.playback()->frame(), 30);
        QTest::mouseClick(window.timeline(), Qt::LeftButton, Qt::NoModifier, QPoint(150, 100));
        QVERIFY(!window.timeline()->selection().isEmpty());
        QTest::keyClick(&window, Qt::Key_S);
        QCOMPARE(window.editor()->project().clips.size(), 2);
        window.editor()->undo();
        QCOMPARE(window.editor()->project().clips.size(), 1);
        window.timeline()->setSnapping(false);
        auto drag = [&](QPoint from, QPoint to) {
            QTest::mousePress(window.timeline(), Qt::LeftButton, Qt::NoModifier, from);
            QMouseEvent move(QEvent::MouseMove, to, window.timeline()->mapToGlobal(to), Qt::NoButton,
                             Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(window.timeline(), &move);
            QTest::mouseRelease(window.timeline(), Qt::LeftButton, Qt::NoModifier, to);
        };
        drag({180, 100}, {240, 100});
        QCOMPARE(window.editor()->project().clips.first().start, 30);
        drag({181, 100}, {201, 100});
        QCOMPARE(window.editor()->project().clips.first().start, 40);
        QCOMPARE(window.editor()->project().clips.first().in, 10);
        QCOMPARE(window.editor()->project().clips.first().length, 80);
        drag({357, 100}, {337, 100});
        QCOMPARE(window.editor()->project().clips.first().length, 70);
        QTest::keyClick(&window, Qt::Key_Delete);
        QVERIFY(window.editor()->project().clips.isEmpty());
        window.editor()->undo();
        QCOMPARE(window.editor()->project().clips.size(), 1);
        Project gaps;
        gaps.media = {media};
        gaps.clips = {{"one", media.id, 0, 0, 15}, {"two", media.id, 30, 30, 15}};
        window.editor()->reset(gaps);
        window.playback()->seek(0);
        QSignalSpy positions(window.playback(), &PlaybackController::positionChanged);
        window.playback()->toggle();
        QTRY_VERIFY_WITH_TIMEOUT(!window.playback()->playing(), 10000);
        QCOMPARE(window.playback()->frame(), 45);
        bool traversedGap = false, traversedSecond = false;
        for (const auto& args : positions) {
            const auto frame = args.first().toLongLong();
            traversedGap |= frame >= 15 && frame < 30;
            traversedSecond |= frame >= 30 && frame < 45;
        }
        QVERIFY(traversedGap);
        QVERIFY(traversedSecond);
        window.editor()->markSaved();
        const auto projectPath = dir.filePath("reopen.sentinel");
        ProjectIO::save(gaps, projectPath);
        window.openProject(projectPath);
        QCOMPARE(window.playback()->frame(), 0);
        QVERIFY(window.editor()->command("Rename", [](Project& p) { p.name = "Saved while reopening"; }));
        bool confirmed = false;
        QTimer::singleShot(0, &window, [&] {
            auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (prompt) {
                confirmed = true;
                prompt->button(QMessageBox::Save)->click();
            }
        });
        window.openProject(projectPath);
        QVERIFY(confirmed);
        QCOMPARE(window.editor()->project().name, QString("Saved while reopening"));
        QCOMPARE(ProjectIO::load(projectPath).name, window.editor()->project().name);
        QVERIFY(!window.editor()->dirty());
        window.close();
    }
};
QTEST_MAIN(UiTests)
#include "UiTests.moc"
