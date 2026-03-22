#include "vision/detectionworker.h"

#include <exception>

#include <opencv2/core.hpp>
#include <opencv2/core/mat.hpp>

#include "common/utils.h"
#include "logger/logmanager.h"
#include "vision/imageprocessor.h"

DetectionWorker::DetectionWorker(LogManager *logManager, QObject *parent)
    : QObject(parent)
    , m_logManager(logManager)
{
}

void DetectionWorker::resetCancellation()
{
    m_cancelRequested.store(false);
}

void DetectionWorker::requestCancel()
{
    m_cancelRequested.store(true);
}

void DetectionWorker::process(const DetectionRequest &request)
{
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

        if (!request.frame.isValid()) {
            emit failed(inspectionId, QStringLiteral("未提供有效的待检测帧。"));
            return;
        }

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

        ImageProcessor imageProcessor;
        if (m_logManager != nullptr) {
            m_logManager->info(
                QStringLiteral("检测"),
                QStringLiteral("算法处理开始：inspectionId=%1 captureId=%2")
                    .arg(inspectionId)
                    .arg(request.frame.meta.captureId),
                false);
        }
        DetectResult result =
            imageProcessor.process(
                sourceImage,
                request.visionParam,
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
            emit canceled(inspectionId);
            return;
        }

        if (m_logManager != nullptr) {
            m_logManager->info(
                QStringLiteral("检测"),
                QStringLiteral("结果图绘制开始：inspectionId=%1 captureId=%2")
                    .arg(inspectionId)
                    .arg(request.frame.meta.captureId),
                false);
        }
        const cv::Mat annotatedImage = utils::drawDetectionOverlay(sourceImage, result);
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
            emit canceled(inspectionId);
            return;
        }

        if (m_logManager != nullptr) {
            m_logManager->debug(QStringLiteral("检测线程"), QStringLiteral("准备发送完成信号。"));
        }
        DetectionOutput output;
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
