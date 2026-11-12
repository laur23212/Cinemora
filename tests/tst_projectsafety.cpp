#include "app/editor/ProjectSafety.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace dafin::project;

// The version stamp and migrations, the atomic write, the rotating backups and the recovery rule
// (src/app/editor/ProjectSafety.*).
class ProjectSafetyTest : public QObject
{
    Q_OBJECT

private:
    static QByteArray read(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static bool put(const QString &path, const QByteArray &data)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        return file.write(data) == data.size();
    }

private slots:
    void filesWithoutAVersionCountAsZeroAndAreStamped()
    {
        QJsonObject root{{"name", "x"}};
        QCOMPARE(versionOf(root), 0);
        bool newer = true;
        migrate(root, &newer);
        QVERIFY(!newer);
        QCOMPARE(versionOf(root), kProjectVersion);
        QCOMPARE(root.value("name").toString(), QString("x"));
        stampVersion(root);
        QCOMPARE(root.value(kProjectVersionKey).toInt(), kProjectVersion);
    }

    void migrationsRunInOrderUpToTheTarget()
    {
        QList<Migration> steps;
        steps.append({0, [](QJsonObject &o) { o.insert("trail", o.value("trail").toString() + "a"); }});
        steps.append({1, [](QJsonObject &o) { o.insert("trail", o.value("trail").toString() + "b"); }});
        // No step for 2 -> 3: it is only restamped.
        steps.append({5, [](QJsonObject &o) { o.insert("trail", "never"); }});

        QJsonObject fromZero;
        migrate(fromZero, steps, 3);
        QCOMPARE(fromZero.value("trail").toString(), QString("ab"));
        QCOMPARE(versionOf(fromZero), 3);

        QJsonObject fromOne{{"projectVersion", 1}};
        migrate(fromOne, steps, 3);
        QCOMPARE(fromOne.value("trail").toString(), QString("b"));
        QCOMPARE(versionOf(fromOne), 3);
    }

    void aNewerFileIsLeftAloneAndFlagged()
    {
        QJsonObject root{{"projectVersion", 99}, {"extra", 1}};
        const QJsonObject before = root;
        bool newer = false;
        migrate(root, &newer);
        QVERIFY(newer);
        QCOMPARE(root, before);
    }

    void atomicWriteReplacesTheFileWholeOrNotAtAll()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("a.bin");
        QVERIFY(atomicWrite(path, "first"));
        QCOMPARE(read(path), QByteArray("first"));
        QVERIFY(atomicWrite(path, "second, longer"));
        QCOMPARE(read(path), QByteArray("second, longer"));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 1); // no temp file left behind

        QString error;
        QVERIFY(!atomicWrite(dir.filePath("missing-folder/a.bin"), "x", &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(read(path), QByteArray("second, longer"));
    }

    void atomicReplaceMovesTheStagedFileOverTheOld()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString dest = dir.filePath("dest");
        const QString staged = dir.filePath("dest.tmp");
        QVERIFY(put(dest, "old"));
        QVERIFY(put(staged, "new"));
        QVERIFY(atomicReplace(staged, dest));
        QCOMPARE(read(dest), QByteArray("new"));
        QVERIFY(!QFile::exists(staged));

        // Nothing staged: the destination stays as it is.
        QVERIFY(!atomicReplace(dir.filePath("nothing"), dest));
        QCOMPARE(read(dest), QByteArray("new"));
    }

    void backupsKeepTheLastFiveVersionsAndTheirOwnBytes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString project = dir.filePath("Film.drift");
        QVERIFY(atomicWrite(project, "v0"));

        QStringList made;
        for (int i = 1; i <= 7; ++i) {
            QTest::qSleep(3); // names carry milliseconds
            const QString backup = backupBeforeOverwrite(project);
            QVERIFY2(!backup.isEmpty(), qPrintable(QString::number(i)));
            made.append(backup);
            // What a save does: replace the file, not rewrite it in place.
            QVERIFY(atomicWrite(project, QByteArray("v") + QByteArray::number(i)));
        }

        const QStringList kept = backupsOf(project);
        QCOMPARE(kept.size(), kKeepBackups);
        QCOMPARE(kept.first(), made.last());                 // newest first
        QCOMPARE(read(kept.first()), QByteArray("v6"));      // the version just before the last save
        QCOMPARE(read(kept.last()), QByteArray("v2"));       // the oldest still kept
        QCOMPARE(read(project), QByteArray("v7"));
        QVERIFY(!QFile::exists(made.first()));               // the first two were trimmed
    }

    void aDamagedFileNeverPushesGoodBackupsOut()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString project = dir.filePath("Film.drift");
        QVERIFY(atomicWrite(project, "good"));
        QVERIFY(!backupBeforeOverwrite(project).isEmpty());
        QCOMPARE(backupsOf(project).size(), 1);

        QVERIFY(atomicWrite(project, "garbage"));
        const auto isGood = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) && file.readAll() == "good";
        };
        QTest::qSleep(3);
        QVERIFY(backupBeforeOverwrite(project, isGood).isEmpty());
        QCOMPARE(backupsOf(project).size(), 1);
        QCOMPARE(read(newestGoodBackup(project, isGood)), QByteArray("good"));
    }

    void theNewestGoodBackupSkipsBadOnes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString project = dir.filePath("Film.drift");
        QVERIFY(atomicWrite(project, "one"));
        const QString first = backupBeforeOverwrite(project);
        QTest::qSleep(3);
        QVERIFY(atomicWrite(project, "two"));
        const QString second = backupBeforeOverwrite(project);
        QVERIFY(!first.isEmpty() && !second.isEmpty());

        const auto notTheNewest = [&second](const QString &path) { return path != second; };
        QCOMPARE(newestGoodBackup(project, notTheNewest), first);
        QCOMPARE(newestGoodBackup(project, [](const QString &) { return false; }), QString());
    }

    void backupsOfNeighbouringProjectsDoNotMix()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString film = dir.filePath("Film.drift");
        const QString film2 = dir.filePath("Film-2.drift");
        QVERIFY(atomicWrite(film, "a"));
        QVERIFY(atomicWrite(film2, "b"));
        QVERIFY(!backupBeforeOverwrite(film).isEmpty());
        QTest::qSleep(3);
        QVERIFY(!backupBeforeOverwrite(film2).isEmpty());
        QCOMPARE(backupsOf(film).size(), 1);
        QCOMPARE(backupsOf(film2).size(), 1);
        QCOMPARE(read(backupsOf(film).first()), QByteArray("a"));
        QCOMPARE(read(backupsOf(film2).first()), QByteArray("b"));
        QCOMPARE(backupBeforeOverwrite(dir.filePath("never-saved.drift")), QString());
    }

    void recoveryIsOfferedOnlyWhenNewerThanTheSavedProject()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString project = dir.filePath("p.drift");
        const QString recovery = dir.filePath("autosave.json");
        QVERIFY(put(project, "p"));
        QVERIFY(put(recovery, "r"));

        const auto touch = [](const QString &path, const QDateTime &when) {
            QFile file(path);
            return file.open(QIODevice::ReadWrite) && file.setFileTime(when, QFileDevice::FileModificationTime);
        };
        const QDateTime now = QDateTime::currentDateTime();

        QVERIFY(touch(project, now.addSecs(-60)));
        QVERIFY(touch(recovery, now));
        QVERIFY(recoveryIsNewer(recovery, project));   // autosaved after the last save

        QVERIFY(touch(project, now));
        QVERIFY(touch(recovery, now.addSecs(-60)));
        QVERIFY(!recoveryIsNewer(recovery, project));  // saved after the snapshot: nothing to restore

        QVERIFY(recoveryIsNewer(recovery, QString()));                      // never saved
        QVERIFY(recoveryIsNewer(recovery, dir.filePath("gone.drift")));     // project file missing
    }
};

QTEST_GUILESS_MAIN(ProjectSafetyTest)
#include "tst_projectsafety.moc"
