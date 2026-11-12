// Dafin: the planning behind the timeline toolbar's Beat markers and Auto ducking buttons.

#include "core/edit/BeatDuckPlan.h"

#include <QtTest>

using namespace drift::beatduck;

class TestBeatDuck : public QObject
{
    Q_OBJECT

private slots:
    void beatGridInsideRange()
    {
        QList<double> beats;
        for (int i = 0; i < 16; ++i)
            beats.append(1.0 + i * 0.5); // 120 bpm from 1 s
        const QList<BeatMarker> m = planBeatMarkers(beats, 4, 1, {}, 2.0, 5.0);
        QCOMPARE(m.size(), 6); // 2.0 .. 4.5
        QCOMPARE(m.first().seconds, 2.0);
        // beat index 2 is the second beat of bar 1 (first downbeat is index 1)
        QVERIFY(!m.first().downbeat);
        QCOMPARE(m.first().bar, 1);
        // index 5 (3.5 s) starts bar 2
        QCOMPARE(m.at(3).seconds, 3.5);
        QVERIFY(m.at(3).downbeat);
        QCOMPARE(m.at(3).bar, 2);
        QCOMPARE(beatMarkerLabel(m.at(3)), beatMarkerPrefix() + QStringLiteral("Bar 2"));
        QCOMPARE(beatMarkerLabel(m.first()), beatMarkerPrefix() + QStringLiteral("Beat"));
    }

    void onsetsWhenNoTempo()
    {
        const QList<OnsetIn> onsets = {{1.0f, 0.9f}, {1.1f, 0.5f}, {2.0f, 0.2f}, {3.0f, 0.6f},
                                       {9.0f, 1.0f}};
        const QList<BeatMarker> m = planBeatMarkers({}, 4, 0, onsets, 0.0, 5.0);
        QCOMPARE(m.size(), 2); // 1.1 crowded by 1.0, 2.0 too weak, 9.0 outside
        QCOMPARE(m.at(0).seconds, 1.0);
        QCOMPARE(m.at(1).seconds, 3.0);
        QCOMPARE(m.at(0).bar, 0);
    }

    void labelsAreRecognised()
    {
        QVERIFY(isBeatMarkerLabel(beatMarkerLabel({1.0, true, 3})));
        QVERIFY(!isBeatMarkerLabel(QStringLiteral("Bookmark")));
    }

    void speechFromLevels()
    {
        // 50 ms windows: silence (-70), a word, a short pause, a word, a click, silence.
        QVector<float> db(60, -70.0f);
        for (int i = 10; i < 20; ++i) db[i] = -20.0f; // 0.5–1.0 s
        for (int i = 23; i < 30; ++i) db[i] = -22.0f; // 1.15–1.5 s, 0.15 s pause → bridged
        db[45] = -15.0f;                              // 50 ms click → dropped
        const QList<Span> s = speechSpans(db, 0.05, 10.0);
        QCOMPARE(s.size(), 1);
        QVERIFY(qAbs(s.first().start - 10.5) < 1e-9);
        QVERIFY(qAbs(s.first().end - 11.5) < 1e-9);
    }

    void quietRecordingIsNotSpeech()
    {
        QVector<float> db(40, -60.0f);
        for (int i = 0; i < 40; i += 2) db[i] = -50.0f; // under the -42 dB floor
        QVERIFY(speechSpans(db, 0.05, 0.0).isEmpty());
    }

    void duckKeysFourPerDip()
    {
        const QList<DuckKey> k = planDuckKeys({{5.0, 7.0}}, 0.0, 20.0, DuckSettings{});
        QCOMPARE(k.size(), 4);
        QCOMPARE(k.at(0).seconds, 4.75);
        QVERIFY(!k.at(0).ducked);
        QVERIFY(k.at(1).ducked && k.at(2).ducked);
        QCOMPARE(k.at(3).seconds, 7.5);
        const QList<Span> w = duckWindows(k);
        QCOMPARE(w.size(), 1);
        QCOMPARE(w.first().start, 4.75);
        QCOMPARE(w.first().end, 7.5);
    }

    void closeDipsMergeInsteadOfPumping()
    {
        // 0.5 s apart < attack + release (0.75): one dip.
        const QList<DuckKey> k = planDuckKeys({{5.0, 6.0}, {6.5, 8.0}}, 0.0, 20.0, DuckSettings{});
        QCOMPARE(k.size(), 4);
        QCOMPARE(k.at(2).seconds, 8.0);
        // 2 s apart: two dips, keys in time order.
        const QList<DuckKey> two = planDuckKeys({{5.0, 6.0}, {8.0, 9.0}}, 0.0, 20.0, DuckSettings{});
        QCOMPARE(two.size(), 8);
        for (int i = 1; i < two.size(); ++i)
            QVERIFY(two.at(i).seconds > two.at(i - 1).seconds);
        QCOMPARE(duckWindows(two).size(), 2);
    }

    void dipAtClipEdgesStartsOrEndsDucked()
    {
        const QList<DuckKey> k = planDuckKeys({{9.0, 13.0}}, 10.0, 12.0, DuckSettings{});
        QCOMPARE(k.size(), 2); // no room for the ramps: ducked from start to end
        QCOMPARE(k.at(0).seconds, 10.0);
        QCOMPARE(k.at(1).seconds, 12.0);
        QVERIFY(k.at(0).ducked && k.at(1).ducked);
        QVERIFY(planDuckKeys({{30.0, 31.0}}, 10.0, 12.0, DuckSettings{}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestBeatDuck)
#include "tst_beatduck.moc"
