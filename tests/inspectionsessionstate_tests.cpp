#include <QtTest>

#include "application/state/inspectionsessionstate.h"

class InspectionSessionStateTests : public QObject
{
    Q_OBJECT

private slots:
    void beginInspectionSetsActiveState();
    void completeInspectionStoresLastCompletedAndClearsActive();
    void abortInspectionClearsActiveState();
    void requestCancelOnlyWorksOnceWhileRunning();
    void continuousInspectionStartStopAreIdempotent();
    void continuousInspectionIntervalIsClamped();
    void resetToDefaultsPreservingInFlightWhenIdle();
    void resetToDefaultsPreservingInFlightWhenRunning();
    void handledFrameAndActiveInspectionMatchingWork();
};

void InspectionSessionStateTests::beginInspectionSetsActiveState()
{
    InspectionSessionState state;
    state.inspectionCancelRequested = true;

    state.beginInspection(QStringLiteral("inspection-001"));

    QCOMPARE(state.activeInspectionId, QStringLiteral("inspection-001"));
    QVERIFY(state.inspectionRunning);
    QVERIFY(!state.inspectionCancelRequested);
}

void InspectionSessionStateTests::completeInspectionStoresLastCompletedAndClearsActive()
{
    InspectionSessionState state;
    state.beginInspection(QStringLiteral("inspection-001"));

    state.completeInspection(QStringLiteral("inspection-001"));

    QCOMPARE(state.lastCompletedInspectionId, QStringLiteral("inspection-001"));
    QVERIFY(state.activeInspectionId.isEmpty());
    QVERIFY(!state.inspectionRunning);
    QVERIFY(!state.inspectionCancelRequested);
}

void InspectionSessionStateTests::abortInspectionClearsActiveState()
{
    InspectionSessionState state;
    state.beginInspection(QStringLiteral("inspection-001"));
    QVERIFY(state.requestCancel());

    state.abortInspection();

    QVERIFY(state.activeInspectionId.isEmpty());
    QVERIFY(!state.inspectionRunning);
    QVERIFY(!state.inspectionCancelRequested);
}

void InspectionSessionStateTests::requestCancelOnlyWorksOnceWhileRunning()
{
    InspectionSessionState state;

    QVERIFY(!state.requestCancel());

    state.beginInspection(QStringLiteral("inspection-001"));
    QVERIFY(state.requestCancel());
    QVERIFY(state.inspectionCancelRequested);
    QVERIFY(!state.requestCancel());
}

void InspectionSessionStateTests::continuousInspectionStartStopAreIdempotent()
{
    InspectionSessionState state;

    QVERIFY(state.startContinuousInspection());
    QVERIFY(state.continuousInspectionEnabled);
    QCOMPARE(state.lastContinuousInspectionFrameIndex, -1);
    QVERIFY(!state.startContinuousInspection());

    state.markHandledFrame(42);
    QVERIFY(state.stopContinuousInspection());
    QVERIFY(!state.continuousInspectionEnabled);
    QCOMPARE(state.lastContinuousInspectionFrameIndex, -1);
    QVERIFY(!state.stopContinuousInspection());
}

void InspectionSessionStateTests::continuousInspectionIntervalIsClamped()
{
    InspectionSessionState state;

    state.setContinuousInspectionIntervalMs(10);
    QCOMPARE(
        state.continuousInspectionIntervalMs,
        InspectionSessionState::kMinimumContinuousInspectionIntervalMs);

    state.setContinuousInspectionIntervalMs(850);
    QCOMPARE(state.continuousInspectionIntervalMs, 850);
}

void InspectionSessionStateTests::resetToDefaultsPreservingInFlightWhenIdle()
{
    InspectionSessionState state;
    state.activeInspectionId = QStringLiteral("stale-id");
    state.lastCompletedInspectionId = QStringLiteral("done-id");
    state.inspectionCancelRequested = true;
    state.continuousInspectionEnabled = true;
    state.continuousInspectionIntervalMs = 2500;
    state.lastContinuousInspectionFrameIndex = 123;

    state.resetToDefaultsPreservingInFlight();

    QVERIFY(state.activeInspectionId.isEmpty());
    QVERIFY(state.lastCompletedInspectionId.isEmpty());
    QVERIFY(!state.inspectionRunning);
    QVERIFY(!state.inspectionCancelRequested);
    QVERIFY(!state.continuousInspectionEnabled);
    QCOMPARE(
        state.continuousInspectionIntervalMs,
        InspectionSessionState::kDefaultContinuousInspectionIntervalMs);
    QCOMPARE(state.lastContinuousInspectionFrameIndex, -1);
}

void InspectionSessionStateTests::resetToDefaultsPreservingInFlightWhenRunning()
{
    InspectionSessionState state;
    state.beginInspection(QStringLiteral("inspection-001"));
    QVERIFY(state.requestCancel());
    state.lastCompletedInspectionId = QStringLiteral("done-id");
    state.continuousInspectionEnabled = true;
    state.continuousInspectionIntervalMs = 2500;
    state.lastContinuousInspectionFrameIndex = 123;

    state.resetToDefaultsPreservingInFlight();

    QCOMPARE(state.activeInspectionId, QStringLiteral("inspection-001"));
    QVERIFY(state.inspectionRunning);
    QVERIFY(state.inspectionCancelRequested);
    QVERIFY(!state.continuousInspectionEnabled);
    QCOMPARE(
        state.continuousInspectionIntervalMs,
        InspectionSessionState::kDefaultContinuousInspectionIntervalMs);
    QCOMPARE(state.lastContinuousInspectionFrameIndex, -1);
    QVERIFY(state.lastCompletedInspectionId.isEmpty());
}

void InspectionSessionStateTests::handledFrameAndActiveInspectionMatchingWork()
{
    InspectionSessionState state;
    state.beginInspection(QStringLiteral("inspection-001"));

    QVERIFY(state.matchesActiveInspection(QStringLiteral("inspection-001")));
    QVERIFY(!state.matchesActiveInspection(QStringLiteral("inspection-002")));
    QVERIFY(!state.hasHandledFrame(7));

    state.markHandledFrame(7);
    QVERIFY(state.hasHandledFrame(7));
    QVERIFY(!state.hasHandledFrame(8));
}

QTEST_GUILESS_MAIN(InspectionSessionStateTests)

#include "inspectionsessionstate_tests.moc"
