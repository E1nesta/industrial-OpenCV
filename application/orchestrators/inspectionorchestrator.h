// 应用层编排：InspectionOrchestrator 负责把输入源请求转换为可执行巡检任务。
// 它只负责任务创建与触发判定，不承担算法执行和 UI 展示。
#pragma once

#include <QString>

#include "application/state/inspectionsessionstate.h"
#include "domain/entities/capturedframe.h"
#include "domain/entities/inputsource.h"
#include "domain/entities/inspectiontask.h"
#include "domain/entities/recipe.h"

class InspectionOrchestrator
{
public:
    // 文件巡检入口：从图片路径构建任务并进入运行态。
    bool startInspectionFromFile(
        const QString &imagePath,
        const Recipe &recipe,
        InspectionSessionState &sessionState,
        InspectionTask *task,
        QString *errorMessage) const;

    // 当前帧巡检入口：校验预览状态后从最新帧构建任务。
    bool startInspectionFromFrame(
        const CapturedFrame &frame,
        const CaptureStatusSnapshot &captureStatus,
        const Recipe &recipe,
        InspectionSessionState &sessionState,
        InspectionTask *task,
        QString *errorMessage) const;

    // 连续巡检启动校验：判断当前输入源与帧状态是否满足连续模式。
    bool canStartContinuousInspection(
        const CaptureStatusSnapshot &captureStatus,
        const CapturedFrame &latestFrame,
        const InspectionSessionState &sessionState,
        QString *errorMessage) const;

    // 连续巡检触发判定：在运行状态稳定时决定是否发起下一次任务。
    bool shouldTriggerContinuousInspection(
        const CaptureStatusSnapshot &captureStatus,
        const CapturedFrame &latestFrame,
        const InspectionSessionState &sessionState) const;

    // 连续巡检去重标记：记录本次已处理帧，避免重复触发。
    void markContinuousInspectionTriggered(
        const CapturedFrame &frame,
        InspectionSessionState &sessionState) const;

private:
    // 创建全局唯一的巡检任务 ID。
    QString createInspectionId() const;

    // 从采集帧构建标准巡检任务对象。
    bool buildInspectionTask(
        const CapturedFrame &frame,
        const QString &inspectionId,
        const Recipe &recipe,
        InspectionTask *task,
        QString *errorMessage) const;

    // 从图片文件构建巡检任务对象。
    bool buildFileInspectionTask(
        const QString &imagePath,
        const QString &inspectionId,
        const Recipe &recipe,
        InspectionTask *task,
        QString *errorMessage) const;
};
