// 应用层编排实现：统一处理文件巡检、当前帧巡检和连续巡检触发判定。
// 本文件只负责任务创建与流程门控，不直接执行视觉算法。
#include "application/orchestrators/inspectionorchestrator.h"

#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QUuid>

#include "common/utils/utils.h"

QString InspectionOrchestrator::createInspectionId() const
{
    // 生成无花括号 UUID，便于日志与外部系统展示。
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool InspectionOrchestrator::startInspectionFromFile(
    const QString &imagePath,
    const Recipe &recipe,
    InspectionSessionState &sessionState,
    InspectionTask *task,
    QString *errorMessage) const
{
    // 入口收敛：先清理上次错误文本，避免 UI 读取到旧错误。
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    // 运行中门控：禁止并发启动巡检任务。
    if (sessionState.inspectionRunning) {
        if (errorMessage != nullptr) {
            *errorMessage = sessionState.inspectionCancelRequested
                                ? QStringLiteral("正在取消上一个巡检任务，请稍后再试。")
                                : QStringLiteral("巡检进行中，请等待当前任务完成。");
        }
        return false;
    }

    // 输入准备：必须提供有效图片路径。
    if (imagePath.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("请先导入一张图片。");
        }
        return false;
    }

    // 核心处理：创建任务并推进会话状态进入运行中。
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
    // 入口收敛：先清理上次错误文本，避免重复提示。
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    // 运行中门控：巡检未完成前不接受新任务。
    if (sessionState.inspectionRunning) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("巡检进行中，请等待当前任务完成。");
        }
        return false;
    }

    // 预览门控：当前帧巡检仅允许在预览态触发。
    if (captureStatus.state != CaptureState::Previewing) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("请先启动预览并保持输入源处于预览状态。");
        }
        return false;
    }

    // 帧有效性校验：没有最新帧时不能构建任务。
    if (!frame.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("当前没有可用帧，请先启动预览。");
        }
        return false;
    }

    // 核心处理：构建任务并推进会话状态。
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
    // 入口收敛：先清空上次失败原因。
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    // 连续巡检只支持实时输入源，不支持单张图片模式。
    if (captureStatus.source.type == InputSourceType::FileImage) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("连续巡检仅支持视频文件或摄像头预览。");
        }
        return false;
    }

    // 连续巡检必须依赖预览状态和可用帧。
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

    // 已开启连续巡检时视为校验通过，调用方可重复触发。
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
    // 连续巡检触发门控：未开启时不触发。
    if (!sessionState.continuousInspectionEnabled) {
        return false;
    }

    // 状态门控：运行中或取消中不触发下一次任务。
    if (sessionState.inspectionRunning || sessionState.inspectionCancelRequested) {
        return false;
    }

    // 输入门控：非预览态或无效帧不触发。
    if (captureStatus.state != CaptureState::Previewing || !latestFrame.isValid()) {
        return false;
    }

    // 去重门控：同一帧仅触发一次巡检。
    return !sessionState.hasHandledFrame(latestFrame.meta.frameIndex);
}

void InspectionOrchestrator::markContinuousInspectionTriggered(
    const CapturedFrame &frame,
    InspectionSessionState &sessionState) const
{
    // 记录已处理帧索引，供下一轮触发判定去重使用。
    sessionState.markHandledFrame(frame.meta.frameIndex);
}

bool InspectionOrchestrator::buildInspectionTask(
    const CapturedFrame &frame,
    const QString &inspectionId,
    const Recipe &recipe,
    InspectionTask *task,
    QString *errorMessage) const
{
    // 入口收敛：构建任务前先清理旧错误。
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    // 输出参数校验：任务输出指针必须有效。
    if (task == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("巡检任务输出指针为空。");
        }
        return false;
    }

    // 输入校验：仅接受有效帧。
    if (!frame.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("待巡检帧无效。");
        }
        return false;
    }

    // 复制帧数据：隔离任务上下文，避免后续预览帧覆盖。
    CapturedFrame copiedFrame = frame;
    copiedFrame.image = frame.image.clone();
    if (!copiedFrame.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("待巡检帧拷贝失败。");
        }
        return false;
    }

    // 结果收敛：写入任务上下文并返回。
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
    // 入口收敛：构建文件任务前先清理错误。
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    // 输出参数校验：任务输出指针必须有效。
    if (task == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("巡检任务输出指针为空。");
        }
        return false;
    }

    // 输入准备：读取本地图片并校验有效性。
    const QImage image(imagePath);
    if (image.isNull()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法读取待巡检图片：%1").arg(imagePath);
        }
        return false;
    }

    // 核心处理：组装帧元信息并转换为 Mat。
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

    // 委托统一路径：复用帧任务构建逻辑，保持任务格式一致。
    return buildInspectionTask(frame, inspectionId, recipe, task, errorMessage);
}
