#pragma once

#include <algorithm>

#include <QString>

class InspectionSessionState
{
public:
    static constexpr int kDefaultContinuousInspectionIntervalMs = 1000;
    static constexpr int kMinimumContinuousInspectionIntervalMs = 100;

    void beginInspection(const QString &inspectionId)
    {
        activeInspectionId = inspectionId;
        inspectionRunning = true;
        inspectionCancelRequested = false;
    }

    void completeInspection(const QString &inspectionId)
    {
        lastCompletedInspectionId = inspectionId;
        clearActiveInspection();
    }

    void abortInspection()
    {
        clearActiveInspection();
    }

    bool requestCancel()
    {
        if (!inspectionRunning || inspectionCancelRequested) {
            return false;
        }

        inspectionCancelRequested = true;
        return true;
    }

    bool startContinuousInspection()
    {
        if (continuousInspectionEnabled) {
            return false;
        }

        continuousInspectionEnabled = true;
        lastContinuousInspectionFrameIndex = -1;
        return true;
    }

    bool stopContinuousInspection()
    {
        if (!continuousInspectionEnabled) {
            return false;
        }

        continuousInspectionEnabled = false;
        lastContinuousInspectionFrameIndex = -1;
        return true;
    }

    void setContinuousInspectionIntervalMs(int intervalMs)
    {
        continuousInspectionIntervalMs =
            std::max(kMinimumContinuousInspectionIntervalMs, intervalMs);
    }

    void resetToDefaultsPreservingInFlight()
    {
        continuousInspectionEnabled = false;
        continuousInspectionIntervalMs = kDefaultContinuousInspectionIntervalMs;
        lastContinuousInspectionFrameIndex = -1;
        lastCompletedInspectionId.clear();

        if (!inspectionRunning) {
            activeInspectionId.clear();
            inspectionCancelRequested = false;
        }
    }

    bool matchesActiveInspection(const QString &inspectionId) const
    {
        return inspectionRunning && !activeInspectionId.isEmpty() && activeInspectionId == inspectionId;
    }

    bool hasHandledFrame(qint64 frameIndex) const
    {
        return frameIndex >= 0 && frameIndex == lastContinuousInspectionFrameIndex;
    }

    void markHandledFrame(qint64 frameIndex)
    {
        lastContinuousInspectionFrameIndex = frameIndex;
    }

    QString activeInspectionId;
    QString lastCompletedInspectionId;
    bool inspectionRunning = false;
    bool inspectionCancelRequested = false;
    bool continuousInspectionEnabled = false;
    int continuousInspectionIntervalMs = kDefaultContinuousInspectionIntervalMs;
    qint64 lastContinuousInspectionFrameIndex = -1;

private:
    void clearActiveInspection()
    {
        activeInspectionId.clear();
        inspectionRunning = false;
        inspectionCancelRequested = false;
    }
};
