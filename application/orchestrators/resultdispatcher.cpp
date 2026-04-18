// 应用层分发实现：统一处理日志、持久化出口、TCP 出口和结果图转换。
// 本文件负责结果出口整合，不承担 UI 控件更新。
#include "application/orchestrators/resultdispatcher.h"

#include "common/logging/logmanager.h"
#include "common/utils/utils.h"

ResultDispatchOutcome ResultDispatcher::dispatch(
    const InspectionOutput &output,
    LogManager *logManager,
    const std::function<void(const InspectionOutput &)> &persistenceSink,
    const std::function<void(const QString &, bool, const DeviceConfig &)> &tcpSink) const
{
    // 输入准备：提取结构化结果，供后续各出口复用。
    const InspectionResult &result = output.result;

    // 阶段 1：记录巡检结论日志，保持主链可追踪。
    if (logManager != nullptr) {
        logManager->info(
            QStringLiteral("检测"),
            QStringLiteral("检测完成：id=%1 result=%2 defects=%3 time=%4 ms")
                .arg(result.inspectionId)
                .arg(utils::boolToResultText(result.isOk))
                .arg(result.defectCount)
                .arg(result.processTimeMs, 0, 'f', 2));
        logManager->info(
            QStringLiteral("检测"),
            QStringLiteral("检测结论：id=%1 %2").arg(result.inspectionId, result.message));
        logManager->info(
            QStringLiteral("记录"),
            QStringLiteral("持久化任务已提交：inspectionId=%1 captureId=%2 source=%3")
                .arg(result.inspectionId)
                .arg(output.request.frame.meta.captureId)
                .arg(output.request.frame.meta.sourceName),
            false);
    }

    // 阶段 2：把输出分发到持久化链路。
    if (persistenceSink) {
        persistenceSink(output);
    }

    // 阶段 3：按任务配置决定是否分发 TCP 结果。
    if (output.request.shouldSendTcpResult) {
        if (logManager != nullptr) {
            logManager->info(
                QStringLiteral("通信"),
                QStringLiteral("TCP 发送任务已提交：inspectionId=%1 result=%2 peer=%3")
                    .arg(result.inspectionId)
                    .arg(utils::boolToResultText(result.isOk))
                    .arg(QStringLiteral("%1:%2")
                             .arg(output.request.tcpDeviceConfig.ip)
                             .arg(output.request.tcpDeviceConfig.port)),
                false);
        }

        if (tcpSink) {
            tcpSink(result.inspectionId, result.isOk, output.request.tcpDeviceConfig);
        }
    }

    // 阶段 4：主线程转换结果图，供 UI 直接显示。
    if (logManager != nullptr) {
        logManager->info(
            QStringLiteral("检测"),
            QStringLiteral("结果图主线程转换开始：inspectionId=%1 size=%2x%3 type=%4")
                .arg(result.inspectionId)
                .arg(output.annotatedImage.cols)
                .arg(output.annotatedImage.rows)
                .arg(output.annotatedImage.type()),
            false);
    }

    // 阶段 5：收敛最终展示输出。
    ResultDispatchOutcome outcome;
    outcome.result = result;
    outcome.resultImage = utils::matToQImage(output.annotatedImage);
    outcome.statusMessage =
        QStringLiteral("检测完成：%1，缺陷 %2 处，耗时 %3 ms")
            .arg(utils::boolToResultText(result.isOk))
            .arg(result.defectCount)
            .arg(result.processTimeMs, 0, 'f', 2);

    if (logManager != nullptr) {
        logManager->info(
            QStringLiteral("检测"),
            QStringLiteral("结果图主线程转换完成：inspectionId=%1 isNull=%2 size=%3x%4")
                .arg(result.inspectionId)
                .arg(outcome.resultImage.isNull() ? QStringLiteral("true") : QStringLiteral("false"))
                .arg(outcome.resultImage.width())
                .arg(outcome.resultImage.height()),
            false);
    }

    // 返回统一分发结果，供控制层更新 UI 状态。
    return outcome;
}
