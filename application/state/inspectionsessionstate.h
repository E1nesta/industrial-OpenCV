// 应用层状态：InspectionSessionState 统一维护巡检会话运行状态。
// 该状态对象用于收敛运行中、取消中、连续巡检和已处理帧信息。
#pragma once

#include <algorithm>

#include <QString>

class InspectionSessionState
{
public:
    // 连续巡检默认间隔，单位毫秒。
    static constexpr int kDefaultContinuousInspectionIntervalMs = 1000;
    // 连续巡检最小允许间隔，单位毫秒。
    static constexpr int kMinimumContinuousInspectionIntervalMs = 100;

    // 进入运行态：记录活动任务并清理取消标记。
    void beginInspection(const QString &inspectionId)
    {
        activeInspectionId = inspectionId;
        inspectionRunning = true;
        inspectionCancelRequested = false;
    }

    // 正常完成：记录最后完成任务并清理运行态。
    void completeInspection(const QString &inspectionId)
    {
        lastCompletedInspectionId = inspectionId;
        clearActiveInspection();
    }

    // 异常中止：直接清理活动任务状态。
    void abortInspection()
    {
        clearActiveInspection();
    }

    // 请求取消：仅在运行中且未取消时生效。
    bool requestCancel()
    {
        if (!inspectionRunning || inspectionCancelRequested) {
            return false;
        }

        inspectionCancelRequested = true;
        return true;
    }

    // 开启连续巡检：重置最近已处理帧索引。
    bool startContinuousInspection()
    {
        if (continuousInspectionEnabled) {
            return false;
        }

        continuousInspectionEnabled = true;
        lastContinuousInspectionFrameIndex = -1;
        return true;
    }

    // 关闭连续巡检：清理连续模式与最近帧索引。
    bool stopContinuousInspection()
    {
        if (!continuousInspectionEnabled) {
            return false;
        }

        continuousInspectionEnabled = false;
        lastContinuousInspectionFrameIndex = -1;
        return true;
    }

    // 更新连续巡检间隔：对非法值做下限钳制。
    void setContinuousInspectionIntervalMs(int intervalMs)
    {
        continuousInspectionIntervalMs =
            std::max(kMinimumContinuousInspectionIntervalMs, intervalMs);
    }

    // 恢复默认配置：保留在途任务，不破坏运行态。
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

    // 活动任务匹配判定：用于回调防串扰。
    bool matchesActiveInspection(const QString &inspectionId) const
    {
        return inspectionRunning && !activeInspectionId.isEmpty() && activeInspectionId == inspectionId;
    }

    // 连续巡检帧去重判定：同一帧只允许处理一次。
    bool hasHandledFrame(qint64 frameIndex) const
    {
        return frameIndex >= 0 && frameIndex == lastContinuousInspectionFrameIndex;
    }

    // 记录最近一次已处理帧索引。
    void markHandledFrame(qint64 frameIndex)
    {
        lastContinuousInspectionFrameIndex = frameIndex;
    }

    // 当前活动任务 ID。
    QString activeInspectionId;
    // 最近一次完成任务 ID。
    QString lastCompletedInspectionId;
    // 当前是否存在在途巡检任务。
    bool inspectionRunning = false;
    // 当前是否已发出取消请求。
    bool inspectionCancelRequested = false;
    // 是否启用连续巡检模式。
    bool continuousInspectionEnabled = false;
    // 连续巡检触发间隔，单位毫秒。
    int continuousInspectionIntervalMs = kDefaultContinuousInspectionIntervalMs;
    // 最近已处理的帧索引，用于触发去重。
    qint64 lastContinuousInspectionFrameIndex = -1;

private:
    // 清理活动巡检状态并恢复空闲态。
    void clearActiveInspection()
    {
        activeInspectionId.clear();
        inspectionRunning = false;
        inspectionCancelRequested = false;
    }
};
