// 基础设施视觉：inspectionworker.cpp 负责后台巡检执行任务。
// 本文件连接任务输入与算法执行，输出统一巡检结果。
#include "infrastructure/vision/inspectionworker.h"

#include <exception>

#include <opencv2/core.hpp>
#include <opencv2/core/mat.hpp>

#include "common/utils/utils.h"
#include "common/logging/logmanager.h"
#include "domain/services/imageprocessor.h"

InspectionWorker::InspectionWorker(LogManager *logManager, QObject *parent)
    : QObject(parent)
    , m_logManager(logManager)
{
}

void InspectionWorker::resetCancellation()
{
    m_cancelRequested.store(false);
}

void InspectionWorker::requestCancel()
{
    m_cancelRequested.store(true);
}

void InspectionWorker::process(const InspectionTask &request)
{
    // 检测 worker 主流程：
    // 请求校验 -> 算法处理 -> 结果绘制 -> 完成/失败/取消回调分发。
    try {
        const QString &inspectionId = request.inspectionId;
        if (m_logManager != nullptr) {
            m_logManager->info(
                QStringLiteral("检测"),
                QStringLiteral("检测任务开始：inspectionId=%1 captureId=%2 source=%3 frameIndex=%4")
                    .arg(inspectionId)
                    .arg(request.frame.meta.captureId)
                    .arg(request.frame.meta.sourceName)
                    .arg(request.frame.meta.frameIndex),
                false);
        }

        // 步骤 1 - 输入校验：无效请求直接回调失败，避免进入算法链。
        if (!request.frame.isValid()) {
            emit failed(inspectionId, QStringLiteral("未提供有效的待检测帧。"));
            return;
        }

        // 取消门控：在重计算前优先退出，避免无意义计算开销。
        if (m_cancelRequested.load()) {
            emit canceled(inspectionId);
            return;
        }

        const cv::Mat sourceImage = request.frame.image;
        if (sourceImage.empty()) {
            emit failed(inspectionId, QStringLiteral("输入帧图像为空。"));
            return;
        }
        if (m_logManager != nullptr) {
            m_logManager->debug(
                QStringLiteral("检测线程"),
                QStringLiteral("图像抓取成功：%1x%2 channels=%3")
                    .arg(sourceImage.cols)
                    .arg(sourceImage.rows)
                    .arg(sourceImage.channels()));
        }

        // 步骤 2 - 算法处理：由 ImageProcessor 执行完整检测链。
        ImageProcessor imageProcessor;
        if (m_logManager != nullptr) {
            m_logManager->info(
                QStringLiteral("检测"),
                QStringLiteral("算法处理开始：inspectionId=%1 captureId=%2")
                    .arg(inspectionId)
                    .arg(request.frame.meta.captureId),
                false);
        }
        InspectionResult result =
            imageProcessor.process(
                sourceImage,
                request.recipe,
                m_logManager,
                [this]() { return m_cancelRequested.load(); });
        if (m_logManager != nullptr) {
            m_logManager->info(
                QStringLiteral("检测"),
                QStringLiteral("算法处理完成：inspectionId=%1 captureId=%2")
                    .arg(inspectionId)
                    .arg(request.frame.meta.captureId),
                false);
        }
        result.inspectionId = inspectionId;
        result.frameMeta = request.frame.meta;
        if (m_logManager != nullptr) {
            m_logManager->info(
                QStringLiteral("检测"),
                QStringLiteral("检测任务完成：inspectionId=%1 captureId=%2 result=%3 defects=%4 timeMs=%5")
                    .arg(result.inspectionId)
                    .arg(request.frame.meta.captureId)
                    .arg(result.isOk ? QStringLiteral("OK") : QStringLiteral("NG"))
                    .arg(result.defectCount)
                    .arg(result.processTimeMs, 0, 'f', 2),
                false);
        }

        if (result.canceled || m_cancelRequested.load()) {
            // 算法阶段收到取消时，优先回调 canceled，让控制层走取消收尾路径。
            emit canceled(inspectionId);
            return;
        }

        // 步骤 3 - 结果图绘制：在 worker 线程内生成标注图，主线程只处理展示转换。
        if (m_logManager != nullptr) {
            m_logManager->info(
                QStringLiteral("检测"),
                QStringLiteral("结果图绘制开始：inspectionId=%1 captureId=%2")
                    .arg(inspectionId)
                    .arg(request.frame.meta.captureId),
                false);
        }
        const cv::Mat annotatedImage = utils::drawInspectionOverlay(sourceImage, result);
        if (m_logManager != nullptr) {
            m_logManager->info(
                QStringLiteral("检测"),
                QStringLiteral("结果图绘制完成：inspectionId=%1 captureId=%2 size=%3x%4 type=%5")
                    .arg(inspectionId)
                    .arg(request.frame.meta.captureId)
                    .arg(annotatedImage.cols)
                    .arg(annotatedImage.rows)
                    .arg(annotatedImage.type()),
                false);
        }
        if (m_logManager != nullptr) {
            m_logManager->debug(QStringLiteral("检测线程"), QStringLiteral("结果图绘制完成。"));
        }
        if (annotatedImage.empty()) {
            emit failed(inspectionId, QStringLiteral("检测结果图生成失败。"));
            return;
        }
        if (m_logManager != nullptr) {
            m_logManager->debug(
                QStringLiteral("检测线程"),
                QStringLiteral("结果图生成成功：%1x%2")
                    .arg(annotatedImage.cols)
                    .arg(annotatedImage.rows));
        }

        if (m_cancelRequested.load()) {
            // 绘制完成后再次检查取消，避免把已取消任务继续分发到下游链路。
            emit canceled(inspectionId);
            return;
        }

        if (m_logManager != nullptr) {
            m_logManager->debug(QStringLiteral("检测线程"), QStringLiteral("准备发送完成信号。"));
        }
        InspectionOutput output;
        output.request = request;
        output.result = result;
        output.annotatedImage = annotatedImage.clone();
        if (m_logManager != nullptr) {
            m_logManager->info(
                QStringLiteral("检测"),
                QStringLiteral("完成信号即将发送：inspectionId=%1 captureId=%2")
                    .arg(inspectionId)
                    .arg(request.frame.meta.captureId),
                false);
        }
        // 步骤 4 - 完成分发：一次性回调 completed，交由控制层分发到 UI/存储/通信链路。
        emit completed(output);
    } catch (const cv::Exception &exception) {
        emit failed(
            request.inspectionId,
            QStringLiteral("检测线程 OpenCV 异常：%1").arg(QString::fromLocal8Bit(exception.what())));
    } catch (const std::exception &exception) {
        emit failed(
            request.inspectionId,
            QStringLiteral("检测线程异常：%1").arg(QString::fromLocal8Bit(exception.what())));
    } catch (...) {
        emit failed(request.inspectionId, QStringLiteral("检测线程发生未知异常。"));
    }
}
