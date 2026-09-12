#include "core/Editor.h"
#include "export/Exporter.h"
#include "media/MediaProbe.h"
#include "project/ProjectIO.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace sentinel;
namespace {
Media fixture() {
    return {"media", QDir::temp().filePath("sentinel-test.mp4"), "test.mp4", 300, true, true, 1280, 720,
            "h264"};
}
Project sequence() {
    Project p;
    p.media = {fixture()};
    p.clips = {{"a", "media", 0, 0, 90}, {"b", "media", 120, 100, 60}};
    return p;
}
QByteArray ffmpeg(const QStringList& args) {
    QProcess p;
    p.start(MediaProbe::executable("ffmpeg"), QStringList{"-v", "error", "-nostdin", "-y"} + args);
    if (!p.waitForStarted() || !p.waitForFinished(60000) || p.exitStatus() != QProcess::NormalExit ||
        p.exitCode() != 0)
        throw std::runtime_error(p.readAllStandardError().toStdString());
    return p.readAllStandardOutput();
}
} // namespace
class EngineTests : public QObject {
    Q_OBJECT
  private slots:
    void resetNormalizesTimeline() {
        auto p = sequence();
        std::swap(p.clips[0], p.clips[1]);
        Editor e;
        e.reset(p);
        QCOMPARE(e.project().clips.first().id, QString("a"));
        QVERIFY(!e.dirty());
    }
    void savesProtectSourceAndBackupPaths() {
        QTemporaryDir dir;
        auto p = sequence();
        const auto source = dir.filePath("source.mp4");
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("original media");
        file.close();
        p.media.first().path = source;
        QVERIFY_EXCEPTION_THROWN(ProjectIO::save(p, source, false), std::runtime_error);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("original media"));
        file.close();
        const auto project = dir.filePath("project.sentinel");
        ProjectIO::save(p, project);
        const auto backup = project + ".bak";
        QFile backupFile(backup);
        QVERIFY(backupFile.open(QIODevice::WriteOnly));
        backupFile.write("protected backup media");
        backupFile.close();
        p.media.first().path = backup;
        QVERIFY_EXCEPTION_THROWN(ProjectIO::save(p, project), std::runtime_error);
        QVERIFY(backupFile.open(QIODevice::ReadOnly));
        QCOMPARE(backupFile.readAll(), QByteArray("protected backup media"));
    }
    void timelineCommands() {
        Editor e;
        e.reset(sequence());
        QSignalSpy errors(&e, &Editor::error);
        QVERIFY(e.split("a", 30));
        QCOMPARE(e.project().clips.size(), 3);
        QCOMPARE(e.project().clips[1].in, 30);
        QCOMPARE(e.project().clips[1].length, 60);
        QVERIFY(e.dirty());
        e.undo();
        QCOMPARE(e.project(), sequence());
        QVERIFY(!e.dirty());
        e.redo();
        QCOMPARE(e.project().clips.size(), 3);
        QVERIFY(!e.move("b", 20));
        QCOMPARE(errors.count(), 1);
        QCOMPARE(e.project().clips.last().start, 120);
        QVERIFY(e.remove("a", true));
        QCOMPARE(e.project().clips.first().start, 0);
        QCOMPARE(e.project().clips.last().start, 90);
        e.undo();
        QVERIFY(e.remove("a"));
        QCOMPARE(e.project().clips.first().start, 30);
    }
    void trimValidationAndHistory() {
        Editor e;
        e.reset(sequence());
        const auto initial = e.project();
        QVERIFY(!e.trim("a", 0, 299, 2));
        QCOMPARE(e.project(), initial);
        QVERIFY(!e.trim("a", -1, 0, 20));
        QVERIFY(!e.split("a", 0));
        QVERIFY(!e.split("a", 90));
        QVERIFY(!e.remove("missing"));
        QVERIFY(e.trim("a", 15, 15, 60));
        QCOMPARE(e.project().clips.first().end(), 75);
        e.markSaved();
        QVERIFY(!e.dirty());
        e.undo();
        QVERIFY(e.dirty());
        e.redo();
        QVERIFY(!e.dirty());
        e.undo();
        QVERIFY(e.move("b", 180));
        QVERIFY(!e.canRedo());
    }
    void importsAndBounds() {
        Editor e;
        QVERIFY(e.importMedia({fixture()}));
        QVERIFY(!e.importMedia({fixture()}));
        QCOMPARE(e.project().media.size(), 1);
        QVERIFY(e.addClip("media", 0));
        QVERIFY(!e.addClip("media", 0));
        QVERIFY(!e.addClip("missing", 400));
        QVERIFY(e.move(e.project().clips.first().id, 300));
        QCOMPARE(e.project().duration(), 600);
        auto p = sequence();
        p.clips.first().start = kMaxFrames;
        QVERIFY_EXCEPTION_THROWN(p.validate(), std::runtime_error);
        p = sequence();
        p.media.first().path = "https://example.com/video.mp4";
        QVERIFY_EXCEPTION_THROWN(p.validate(), std::runtime_error);
    }
    void roundTripAndRecovery() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto p = sequence();
        p.name = "Unicode — पर्वत";
        const auto path = dir.filePath("project.sentinel");
        ProjectIO::save(p, path);
        QCOMPARE(ProjectIO::load(path), p);
        auto next = p;
        next.clips.first().length = 45;
        ProjectIO::save(next, path);
        QCOMPARE(ProjectIO::load(path), next);
        QCOMPARE(ProjectIO::load(path + ".bak"), p);
        ProjectIO::save(next, dir.filePath("recovery.sentinel"));
        QCOMPARE(ProjectIO::load(dir.filePath("recovery.sentinel")), next);
        QFile broken(path);
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("{broken");
        broken.close();
        QVERIFY_EXCEPTION_THROWN(ProjectIO::load(path), std::runtime_error);
        QCOMPARE(ProjectIO::load(path + ".bak"), p);
    }
    void malformedProjects() {
        QVERIFY_EXCEPTION_THROWN(ProjectIO::decode("{}"), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(ProjectIO::decode(QByteArray(16 * 1024 * 1024 + 1, 'x')),
                                 std::runtime_error);
        auto root = QJsonDocument::fromJson(ProjectIO::encode(sequence())).object();
        root["version"] = 2;
        QVERIFY_EXCEPTION_THROWN(ProjectIO::decode(QJsonDocument(root).toJson()), std::runtime_error);
        root["version"] = 1;
        root["fps"] = 29.97;
        QVERIFY_EXCEPTION_THROWN(ProjectIO::decode(QJsonDocument(root).toJson()), std::runtime_error);
        auto p = sequence();
        p.media.push_back(p.media.first());
        QVERIFY_EXCEPTION_THROWN(p.validate(), std::runtime_error);
        p = sequence();
        p.clips.first().mediaId = "missing";
        QVERIFY_EXCEPTION_THROWN(p.validate(), std::runtime_error);
    }
    void mediaAndExportIntegration() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto red = dir.filePath("red ' ; $ video.mp4"), blue = dir.filePath("blue.mp4"),
                   audio = dir.filePath("tone.wav");
        ffmpeg({"-f", "lavfi", "-i", "color=c=red:s=320x180:r=30:d=2", "-f", "lavfi", "-i",
                "sine=frequency=440:duration=2", "-c:v", "libx264", "-threads", "1", "-pix_fmt", "yuv420p",
                "-c:a", "aac", "-shortest", red});
        ffmpeg({"-f", "lavfi", "-i", "color=c=blue:s=160x120:r=24:d=2", "-c:v", "libx264", "-threads", "1",
                "-pix_fmt", "yuv420p", blue});
        ffmpeg({"-f", "lavfi", "-i", "sine=frequency=880:duration=2", audio});
        const auto r = MediaProbe::probe(red), b = MediaProbe::probe(blue), a = MediaProbe::probe(audio);
        QVERIFY(r.video && r.audio);
        QVERIFY(b.video && !b.audio);
        QVERIFY(!a.video && a.audio);
        QCOMPARE(r.frames, 60);
        Project p;
        p.media = {r, b, a};
        p.clips = {{"red", r.id, 0, 15, 30}, {"blue", b.id, 60, 0, 30}, {"audio", a.id, 90, 15, 30}};
        const auto out = dir.filePath("export.mp4");
        std::atomic_bool cancel = false;
        int progress = 0;
        Exporter::render(p, out, cancel, [&](int n, const QString&) { progress = n; });
        QCOMPARE(progress, 100);
        const auto result = MediaProbe::probe(out);
        QCOMPARE(result.width, 1280);
        QCOMPARE(result.height, 720);
        QVERIFY(result.video && result.audio);
        QVERIFY(std::abs(result.frames - 120) <= 1);
        auto pixel = [&](const QString& sec) {
            return ffmpeg({"-ss", sec, "-i", out, "-frames:v", "1", "-vf", "crop=2:2:640:360", "-f",
                           "rawvideo", "-pix_fmt", "rgb24", "pipe:1"});
        };
        auto redPixel = pixel("0.5");
        QVERIFY(redPixel.size() >= 3);
        QVERIFY(quint8(redPixel[0]) > 200 && quint8(redPixel[2]) < 40);
        auto black = pixel("1.5");
        QVERIFY(black.size() >= 3);
        QVERIFY(quint8(black[0]) < 10 && quint8(black[1]) < 10 && quint8(black[2]) < 10);
        auto bluePixel = pixel("2.5");
        QVERIFY(bluePixel.size() >= 3);
        QVERIFY(quint8(bluePixel[2]) > 200 && quint8(bluePixel[0]) < 40);
        auto pcm = ffmpeg({"-ss", "0.2", "-i", out, "-t", "0.2", "-vn", "-f", "s16le", "-ac", "1", "pipe:1"});
        long long energy = 0;
        for (qsizetype i = 0; i + 1 < pcm.size(); i += 2) {
            const qint16 value = qint16(quint8(pcm[i]) | (quint16(quint8(pcm[i + 1])) << 8));
            energy += std::abs(int(value));
        }
        QVERIFY(energy > 100000);
        auto silence =
            ffmpeg({"-ss", "1.2", "-i", out, "-t", "0.2", "-vn", "-f", "s16le", "-ac", "1", "pipe:1"});
        for (auto byte : silence)
            QCOMPARE(byte, char(0));
        cancel = true;
        QVERIFY_EXCEPTION_THROWN(Exporter::render(p, dir.filePath("canceled.mp4"), cancel),
                                 std::runtime_error);
        QVERIFY(!QFileInfo::exists(dir.filePath("canceled.mp4")));
        cancel = false;
        QVERIFY_EXCEPTION_THROWN(Exporter::render(p, red, cancel), std::runtime_error);
        QCOMPARE(MediaProbe::probe(red).frames, 60);
        QFile::remove(blue);
        QVERIFY_EXCEPTION_THROWN(Exporter::render(p, out, cancel), std::runtime_error);
        QCOMPARE(MediaProbe::probe(out).frames, result.frames);
        QVERIFY_EXCEPTION_THROWN(MediaProbe::probe(dir.filePath("missing.mp4")), std::runtime_error);
        QFile invalid(dir.filePath("broken.mp4"));
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        invalid.write("not a movie");
        invalid.close();
        QVERIFY_EXCEPTION_THROWN(MediaProbe::probe(invalid.fileName()), std::runtime_error);
    }
};
QTEST_GUILESS_MAIN(EngineTests)
#include "EngineTests.moc"
