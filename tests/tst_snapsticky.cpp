#include "core/edit/TimelineOps.h"

#include <QtTest>

using namespace drift;

class SnapStickyTest : public QObject {
    Q_OBJECT
private slots:
    void holdsUntilBreakAway()
    {
        SnapTargets t;
        t.sorted = {1'000'000, 1'100'000};
        TimeUs held = -1;
        // Pulled in: 990'000 is within the 50'000 threshold of 1'000'000.
        QCOMPARE(snapTimeToSticky(t, 990'000, true, 50'000, 75'000, held), TimeUs(1'000'000));
        QCOMPARE(held, TimeUs(1'000'000));
        // 930'000 is outside the threshold but inside break-away: still held.
        QCOMPARE(snapTimeToSticky(t, 930'000, true, 50'000, 75'000, held), TimeUs(1'000'000));
        // 900'000 is beyond break-away: released.
        QCOMPARE(snapTimeToSticky(t, 900'000, true, 50'000, 75'000, held), TimeUs(900'000));
        QCOMPARE(held, TimeUs(-1));
    }
    void noJitterBetweenNearTargets()
    {
        SnapTargets t;
        t.sorted = {1'000'000, 1'060'000};
        TimeUs held = -1;
        QCOMPARE(snapTimeToSticky(t, 1'020'000, true, 50'000, 75'000, held), TimeUs(1'000'000));
        // 1'035'000 is nearer 1'060'000 but we stay on the held target.
        QCOMPARE(snapTimeToSticky(t, 1'035'000, true, 50'000, 75'000, held), TimeUs(1'000'000));
    }
    void disabledReleases()
    {
        SnapTargets t;
        t.sorted = {1'000'000};
        TimeUs held = 1'000'000;
        QCOMPARE(snapTimeToSticky(t, 995'000, false, 50'000, 75'000, held), TimeUs(995'000));
        QCOMPARE(held, TimeUs(-1));
    }
};

QTEST_APPLESS_MAIN(SnapStickyTest)
#include "tst_snapsticky.moc"
