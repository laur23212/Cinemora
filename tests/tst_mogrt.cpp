// Dafin: .mogrt template titles — what can be checked without MOGRT Converter itself.
//
//  - a template title's state (MogrtSource) survives the project file;
//  - a template's Essential Graphics controls are read the way the converter reads them;
//  - MogrtRenderer's queue: priorities, the --values file, the temporary-name-then-rename write,
//    cancelling and errors, against a stand-in converter (a shell script, so Unix only).

#include <QtTest>

#include "TestZip.h"
#include "app/online/library/MogrtInfo.h"
#include "app/online/library/MogrtRenderer.h"
#include "core/project/Project.h"
#include "core/project/sources/MogrtSource.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace drift;
using dafin::assets::MogrtRenderer;

namespace {

QJsonObject localized(const QString &text)
{
    return QJsonObject{{QStringLiteral("strDB"),
                        QJsonArray{QJsonObject{{QStringLiteral("localeString"), QStringLiteral("en_US")},
                                               {QStringLiteral("str"), text}}}}};
}

QJsonObject control(const QJsonValue &id, int type, const QString &name, const QJsonValue &value)
{
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("type"), type},
                       {QStringLiteral("uiName"), localized(name)},
                       {QStringLiteral("value"), value}};
}

// A minimal After Effects template: definition.json is all the readers look at.
QByteArray definitionJson()
{
    QJsonObject slider = control(QStringLiteral("s1"), 2, QStringLiteral("Size"), 50);
    slider.insert(QStringLiteral("min"), 0);
    slider.insert(QStringLiteral("max"), 100);
    const QJsonArray controls{
        control(QStringLiteral("g1"), 8, QStringLiteral("Main"), QStringLiteral("Main")),
        control(QStringLiteral("t1"), 6, QStringLiteral("Name"), localized(QStringLiteral("John Doe\r"))),
        slider,
        control(QStringLiteral("c1"), 4, QStringLiteral("Colour"), QJsonArray{1, 0.5, 0, 1, 9}),
        control(QStringLiteral("b1"), 1, QStringLiteral("Shadow"), true),
        control(QStringLiteral("p1"), 5, QStringLiteral("Position"),
                QJsonObject{{QStringLiteral("x"), 960}, {QStringLiteral("y"), 540}}),
        control(7, 7, QStringLiteral("Style"), 1),
    };
    const QJsonObject root{{QStringLiteral("authorApp"), QStringLiteral("aefx")},
                           {QStringLiteral("sourceInfoLocalized"),
                            QJsonObject{{QStringLiteral("en_US"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Lower Third")}}}}},
                           {QStringLiteral("clientControls"), controls}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QJsonObject controlById(const QJsonArray &controls, const QString &id)
{
    for (const QJsonValue &v : controls) {
        if (v.toObject().value(QStringLiteral("id")).toString() == id)
            return v.toObject();
    }
    return {};
}

} // namespace

class MogrtTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("dafin-tests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_mogrt"));
    }

    void sourceSurvivesTheProjectFile()
    {
        Project project;
        Track track;
        track.type = TrackType::Video;
        Clip clip;
        clip.id = QStringLiteral("clip-1");
        clip.type = ClipType::Video;
        clip.path = QStringLiteral("/cache/abc-preview.mov");
        clip.timelineStart = 0;
        clip.timelineDuration = 4000000;
        clip.srcOut = 4000000;
        clip.mogrt.templateId = QStringLiteral("0123456789abcdef");
        clip.mogrt.templatePath = QStringLiteral("/store/0123456789abcdef.mogrt");
        clip.mogrt.name = QStringLiteral("Lower Third");
        clip.mogrt.authorApp = QStringLiteral("aefx");
        clip.mogrt.controls = QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("t1")},
                                                     {QStringLiteral("type"), QStringLiteral("text")},
                                                     {QStringLiteral("label"), QStringLiteral("Name")},
                                                     {QStringLiteral("default"), QStringLiteral("John Doe")}}};
        clip.mogrt.values = QJsonObject{{QStringLiteral("t1"), QStringLiteral("Ana\nPopescu")}};
        clip.mogrt.width = 1920;
        clip.mogrt.height = 1080;
        clip.mogrt.fps = 29.97;
        clip.mogrt.templateDurationUs = 5000000;
        clip.mogrt.renderKey = QStringLiteral("abc");
        clip.mogrt.renderQuality = QStringLiteral("preview");
        clip.mogrt.status = QStringLiteral("warnings");
        clip.mogrt.warnings = {QStringLiteral("The effect “Glow” is not supported yet and was left out.")};
        track.clips.append(clip);
        Clip plain;
        plain.id = QStringLiteral("clip-2");
        plain.type = ClipType::Video;
        plain.path = QStringLiteral("/media/a.mp4");
        plain.timelineStart = 4000000;
        plain.timelineDuration = 1000000;
        plain.srcOut = 1000000;
        track.clips.append(plain);
        project.tracks().append(track);

        const QJsonObject json = project.toJson();
        QString error;
        Project reopened = Project::fromJson(json, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        // A new project may come with tracks of its own: find ours by its clips.
        QList<Clip> clips;
        for (const Track &t : reopened.tracks()) {
            if (!t.clips.isEmpty() && t.clips.first().id == clip.id)
                clips = t.clips;
        }
        QCOMPARE(clips.size(), 2);

        const MogrtSource &m = clips.at(0).mogrt;
        QVERIFY(!m.isEmpty());
        QCOMPARE(m.templateId, clip.mogrt.templateId);
        QCOMPARE(m.templatePath, clip.mogrt.templatePath);
        QCOMPARE(m.name, clip.mogrt.name);
        QCOMPARE(m.authorApp, clip.mogrt.authorApp);
        QCOMPARE(m.controls, clip.mogrt.controls);
        QCOMPARE(m.values, clip.mogrt.values);
        QCOMPARE(m.width, 1920);
        QCOMPARE(m.height, 1080);
        QCOMPARE(m.fps, 29.97);
        QCOMPARE(m.templateDurationUs, int64_t(5000000));
        QCOMPARE(m.renderKey, clip.mogrt.renderKey);
        QCOMPARE(m.renderQuality, clip.mogrt.renderQuality);
        QCOMPARE(m.status, clip.mogrt.status);
        QCOMPARE(m.warnings, clip.mogrt.warnings);

        // An ordinary clip stays ordinary, and writes nothing for it.
        QVERIFY(clips.at(1).mogrt.isEmpty());
        const QByteArray saved = QJsonDocument(json).toJson(QJsonDocument::Compact);
        QCOMPARE(saved.count("\"mogrt\""), qsizetype(1));
    }

    void readsTheTemplatesControls()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("lower-third.mogrt"));
        QVERIFY(writeStoredZip(path, {{QStringLiteral("definition.json"), definitionJson()}}));

        QString error;
        const std::optional<dafin::assets::mogrt::Info> info = dafin::assets::mogrt::read(path, &error);
        QVERIFY2(info.has_value(), qPrintable(error));
        QCOMPARE(info->title, QStringLiteral("Lower Third"));
        QCOMPARE(info->authorApp, QStringLiteral("aefx"));

        const QJsonArray &controls = info->controls;
        QCOMPARE(controls.size(), 7);
        QCOMPARE(controlById(controls, QStringLiteral("g1")).value(QStringLiteral("type")).toString(), QStringLiteral("group"));

        const QJsonObject text = controlById(controls, QStringLiteral("t1"));
        QCOMPARE(text.value(QStringLiteral("type")).toString(), QStringLiteral("text"));
        QCOMPARE(text.value(QStringLiteral("label")).toString(), QStringLiteral("Name"));
        // After Effects ends lines with \r; the trailing one is not part of the text.
        QCOMPARE(text.value(QStringLiteral("default")).toString(), QStringLiteral("John Doe"));

        const QJsonObject slider = controlById(controls, QStringLiteral("s1"));
        QCOMPARE(slider.value(QStringLiteral("type")).toString(), QStringLiteral("slider"));
        QCOMPARE(slider.value(QStringLiteral("default")).toDouble(), 50.0);
        QCOMPARE(slider.value(QStringLiteral("min")).toDouble(), 0.0);
        QCOMPARE(slider.value(QStringLiteral("max")).toDouble(), 100.0);

        // RGBA only, whatever else the template stores after it.
        QCOMPARE(controlById(controls, QStringLiteral("c1")).value(QStringLiteral("default")).toArray(),
                 (QJsonArray{1, 0.5, 0, 1}));
        QCOMPARE(controlById(controls, QStringLiteral("b1")).value(QStringLiteral("default")).toBool(), true);
        QCOMPARE(controlById(controls, QStringLiteral("p1")).value(QStringLiteral("default")).toArray(),
                 (QJsonArray{960, 540}));

        // A numeric id comes back as text, so it can key the values like the others.
        const QJsonObject dropdown = controlById(controls, QStringLiteral("7"));
        QCOMPARE(dropdown.value(QStringLiteral("type")).toString(), QStringLiteral("dropdown"));
    }

    void rendererQueuesWritesAndCancels()
    {
#ifdef Q_OS_WIN
        QSKIP("The stand-in converter is a shell script.");
#else
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString log = dir.filePath(QStringLiteral("calls.log"));
        const QString script = dir.filePath(QStringLiteral("mogrt"));
        {
            QFile f(script);
            QVERIFY(f.open(QIODevice::WriteOnly));
            // Records the order of runs and the values it was given, writes its -o file, answers
            // in JSON like MOGRT Converter with --json, and fails for a template named *fail*.
            // Plain strings, not a raw literal: moc must still find the class in this file.
            const QStringList lines{
                QStringLiteral("#!/bin/sh"),
                QStringLiteral("verb=\"$1\"; shift; tpl=\"$1\"; shift"),
                QStringLiteral("out=\"\"; values=\"\""),
                QStringLiteral("while [ $# -gt 0 ]; do"),
                QStringLiteral("  case \"$1\" in"),
                QStringLiteral("    -o) out=\"$2\"; shift;;"),
                QStringLiteral("    --values) values=\"$2\"; shift;;"),
                QStringLiteral("  esac; shift"),
                QStringLiteral("done"),
                QStringLiteral("v=\"\"; [ -n \"$values\" ] && v=$(cat \"$values\")"),
                QStringLiteral("echo \"$verb $(basename \"$out\") $v\" >> \"%1\""),
                QStringLiteral("case \"$tpl\" in *fail*) echo '{\"ok\": false, \"error\": \"Nur After-Effects-MOGRTs werden unterstuetzt (dieses ist: ppro).\"}'; exit 2;; esac"),
                QStringLiteral("sleep 0.2"),
                QStringLiteral("printf '  Frame 3/3  (0s)\\r' >&2"),
                QStringLiteral("[ -n \"$out\" ] && echo \"rendered\" > \"$out\""),
                QStringLiteral("echo \"{\\\"ok\\\": true, \\\"output\\\": \\\"$out\\\", \\\"warnings\\\": [\\\"Font 'Gotham' nicht gefunden\\\"]}\""),
            };
            f.write(lines.join(QLatin1Char('\n')).arg(log).toUtf8() + "\n");
            f.close();
            QVERIFY(f.setPermissions(f.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser));
        }
        const QString templ = dir.filePath(QStringLiteral("t.mogrt"));
        const QString failing = dir.filePath(QStringLiteral("fail.mogrt"));
        for (const QString &p : {templ, failing}) {
            QFile f(p);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
        }

        MogrtRenderer renderer;
        QVERIFY(renderer.useProgram(script));
        QVERIFY(renderer.available());
        QVERIFY(!renderer.signature().isEmpty());

        const QString cache = dir.filePath(QStringLiteral("cache"));
        MogrtRenderer::Request low;
        low.verb = QStringLiteral("render");
        low.templatePath = templ;
        low.values = QJsonObject{{QStringLiteral("t1"), QStringLiteral("Ana")}};
        low.output = cache + QStringLiteral("/low.mov");
        low.durationSeconds = 4;
        MogrtRenderer::Request high = low;
        high.output = cache + QStringLiteral("/high.mov");
        high.priority = 5;
        MogrtRenderer::Request third = low;
        third.output = cache + QStringLiteral("/third.mov");
        MogrtRenderer::Request bad = low;
        bad.templatePath = failing;
        bad.output = cache + QStringLiteral("/bad.mov");
        MogrtRenderer::Request cancelled = low;
        cancelled.output = cache + QStringLiteral("/cancelled.mov");
        cancelled.priority = -5;

        QHash<QString, QString> errors;
        QHash<QString, QJsonObject> results;
        connect(&renderer, &MogrtRenderer::jobFinished, this,
                [&](const QString &key, bool ok, const QJsonObject &result, const QString &error) {
                    if (ok)
                        results.insert(key, result);
                    else
                        errors.insert(key, error);
                });

        // Submitted last but one, run first: two run at a time, the rest wait in priority order.
        const QString kLow = renderer.submit(low);
        const QString kThird = renderer.submit(third);
        const QString kBad = renderer.submit(bad);
        const QString kHigh = renderer.submit(high);
        const QString kCancelled = renderer.submit(cancelled);
        QVERIFY(renderer.isActive(kCancelled));
        renderer.cancel(kCancelled);

        QTRY_COMPARE_WITH_TIMEOUT(results.size() + errors.size(), 5, 20000);

        QVERIFY(results.contains(kLow) && results.contains(kHigh) && results.contains(kThird));
        QCOMPARE(results.value(kHigh).value(QStringLiteral("output")).toString(), high.output);
        QCOMPARE(errors.value(kCancelled), MogrtRenderer::tr("Cancelled."));
        QVERIFY(errors.value(kBad).contains(QLatin1String("Premiere")));

        // Finished files under their own names; no temporary or values file left behind.
        for (const QString &p : {high.output, low.output, third.output})
            QVERIFY2(QFile::exists(p), qPrintable(p));
        QVERIFY(!QFile::exists(cancelled.output) && !QFile::exists(bad.output));
        const QStringList left = QDir(cache).entryList(QDir::Files);
        for (const QString &name : left)
            QVERIFY2(!name.contains(QLatin1String(".part-")) && !name.startsWith(QLatin1String("values-")), qPrintable(name));

        // The first two runs are the high-priority one and the earliest of the rest; the values
        // reached the converter as JSON.
        QFile calls(log);
        QVERIFY(calls.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(calls.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 4);
        const QString firstTwo = lines.at(0) + QLatin1Char(' ') + lines.at(1);
        QVERIFY2(firstTwo.contains(QLatin1String("render high.part-")) && firstTwo.contains(QLatin1String("render low.part-")),
                 qPrintable(firstTwo));
        QVERIFY2(lines.at(0).contains(QLatin1String("{\"t1\":\"Ana\"}")), qPrintable(lines.at(0)));
        for (const QString &line : lines)
            QVERIFY2(!line.contains(QLatin1String("cancelled")), qPrintable(line));
#endif
    }
};

QTEST_GUILESS_MAIN(MogrtTest)
#include "tst_mogrt.moc"
