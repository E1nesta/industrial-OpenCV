#include "application/orchestrators/resultdispatcher.h"

#include "common/logging/logmanager.h"
#include "common/utils/utils.h"

ResultDispatchOutcome ResultDispatcher::dispatch(
    const InspectionOutput &output,
    LogManager *logManager,
    const std::function<void(const InspectionOutput &)> &persistenceSink,
    const std::function<void(const QString &, bool, const DeviceConfig &)> &tcpSink) const
{
    const InspectionResult &result = output.result;

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

    if (persistenceSink) {
        persistenceSink(output);
    }

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

    return outcome;
}
