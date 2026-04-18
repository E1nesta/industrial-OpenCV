#include "application/orchestrators/inspectionorchestrator.h"

#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QUuid>

#include "common/utils/utils.h"

QString InspectionOrchestrator::createInspectionId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool InspectionOrchestrator::startInspectionFromFile(
    const QString &imagePath,
    const Recipe &recipe,
    InspectionSessionState &sessionState,
    InspectionTask *task,
    QString *errorMessage) const
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    if (sessionState.inspectionRunning) {
        if (errorMessage != nullptr) {
            *errorMessage = sessionState.inspectionCancelRequested
                                ? QStringLiteral("正在取消上一个巡检任务，请稍后再试。")
                                : QStringLiteral("巡检进行中，请等待当前任务完成。");
        }
        return false;
    }

    if (imagePath.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("请先导入一张图片。");
        }
        return false;
    }

    const QString inspectionId = createInspectionId();
    if (!buildFileInspectionTask(imagePath, inspectionId, recipe, task, errorMessage)) {
        return false;
    }

    sessionState.beginInspection(inspectionId);
    return true;
}

bool InspectionOrchestrator::startInspectionFromFrame(
    const CapturedFrame &frame,
    const CaptureStatusSnapshot &captureStatus,
    const Recipe &recipe,
    InspectionSessionState &sessionState,
    InspectionTask *task,
    QString *errorMessage) const
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    if (sessionState.inspectionRunning) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("巡检进行中，请等待当前任务完成。");
        }
        return false;
    }

    if (captureStatus.state != CaptureState::Previewing) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("请先启动预览并保持输入源处于预览状态。");
        }
        return false;
    }

    if (!frame.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("当前没有可用帧，请先启动预览。");
        }
        return false;
    }

    const QString inspectionId = createInspectionId();
    if (!buildInspectionTask(frame, inspectionId, recipe, task, errorMessage)) {
        return false;
    }

    sessionState.beginInspection(inspectionId);
    return true;
}

bool InspectionOrchestrator::canStartContinuousInspection(
    const CaptureStatusSnapshot &captureStatus,
    const CapturedFrame &latestFrame,
    const InspectionSessionState &sessionState,
    QString *errorMessage) const
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    if (captureStatus.source.type == InputSourceType::FileImage) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("连续巡检仅支持视频文件或摄像头预览。");
        }
        return false;
    }

    if (captureStatus.state != CaptureState::Previewing) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("开启连续巡检失败：请先启动预览。");
        }
        return false;
    }

    if (!latestFrame.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("开启连续巡检失败：当前没有可用帧，请稍候再试。");
        }
        return false;
    }

    if (sessionState.continuousInspectionEnabled) {
        return true;
    }

    return true;
}

bool InspectionOrchestrator::shouldTriggerContinuousInspection(
    const CaptureStatusSnapshot &captureStatus,
    const CapturedFrame &latestFrame,
    const InspectionSessionState &sessionState) const
{
    if (!sessionState.continuousInspectionEnabled) {
        return false;
    }

    if (sessionState.inspectionRunning || sessionState.inspectionCancelRequested) {
        return false;
    }

    if (captureStatus.state != CaptureState::Previewing || !latestFrame.isValid()) {
        return false;
    }

    return !sessionState.hasHandledFrame(latestFrame.meta.frameIndex);
}

void InspectionOrchestrator::markContinuousInspectionTriggered(
    const CapturedFrame &frame,
    InspectionSessionState &sessionState) const
{
    sessionState.markHandledFrame(frame.meta.frameIndex);
}

bool InspectionOrchestrator::buildInspectionTask(
    const CapturedFrame &frame,
    const QString &inspectionId,
    const Recipe &recipe,
    InspectionTask *task,
    QString *errorMessage) const
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    if (task == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("巡检任务输出指针为空。");
        }
        return false;
    }

    if (!frame.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("待巡检帧无效。");
        }
        return false;
    }

    CapturedFrame copiedFrame = frame;
    copiedFrame.image = frame.image.clone();
    if (!copiedFrame.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("待巡检帧拷贝失败。");
        }
        return false;
    }

    task->inspectionId = inspectionId;
    task->frame = std::move(copiedFrame);
    task->recipe = recipe;
    return true;
}

bool InspectionOrchestrator::buildFileInspectionTask(
    const QString &imagePath,
    const QString &inspectionId,
    const Recipe &recipe,
    InspectionTask *task,
    QString *errorMessage) const
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    if (task == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("巡检任务输出指针为空。");
        }
        return false;
    }

    const QImage image(imagePath);
    if (image.isNull()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法读取待巡检图片：%1").arg(imagePath);
        }
        return false;
    }

    CapturedFrame frame;
    frame.meta.captureId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    frame.meta.sourceType = InputSourceType::FileImage;
    frame.meta.sourcePath = imagePath;
    frame.meta.sourceName = QFileInfo(imagePath).fileName();
    frame.meta.frameIndex = 0;
    frame.meta.capturedAt = QDateTime::currentDateTime();
    frame.image = utils::qImageToMat(image);
    if (!frame.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("待巡检图片转换失败：%1").arg(imagePath);
        }
        return false;
    }

    return buildInspectionTask(frame, inspectionId, recipe, task, errorMessage);
}
