#include "vision/detectionworker.h"

#include <exception>

#include <QImage>

#include <opencv2/core.hpp>
#include <opencv2/core/mat.hpp>

#include "camera/cameramanager.h"
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

void DetectionWorker::process(const QString &inspectionId, const QString &imagePath, const VisionParam &param)
{
    try {
        if (m_logManager != nullptr) {
            m_logManager->debug(
                QStringLiteral("检测线程"),
                QStringLiteral("开始处理图片：id=%1 path=%2").arg(inspectionId).arg(imagePath));
        }

        if (imagePath.isEmpty()) {
            emit failed(QStringLiteral("未提供待检测图片路径。"));
            return;
        }

        if (m_cancelRequested.load()) {
            emit canceled();
            return;
        }

        CameraManager cameraManager;
        if (!cameraManager.loadLocalImage(imagePath)) {
            emit failed(QStringLiteral("无法加载本地图片：%1").arg(imagePath));
            return;
        }
        if (m_logManager != nullptr) {
            m_logManager->debug(QStringLiteral("检测线程"), QStringLiteral("本地图片加载成功。"));
        }

        const cv::Mat sourceImage = cameraManager.grabImage();
        if (sourceImage.empty()) {
            emit failed(QStringLiteral("图片载入成功但图像为空：%1").arg(imagePath));
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
        DetectResult result =
            imageProcessor.process(sourceImage, param, m_logManager, [this]() { return m_cancelRequested.load(); });
        result.inspectionId = inspectionId;
        result.imagePath = imagePath;
        if (m_logManager != nullptr) {
            m_logManager->debug(
                QStringLiteral("检测线程"),
                QStringLiteral("图像处理完成：id=%1 result=%2 defects=%3")
                    .arg(result.inspectionId)
                    .arg(result.isOk ? QStringLiteral("OK") : QStringLiteral("NG"))
                    .arg(result.defectCount));
        }

        if (result.canceled || m_cancelRequested.load()) {
            emit canceled();
            return;
        }

        const cv::Mat annotatedImage = utils::drawDetectionOverlay(sourceImage, result);
        if (m_logManager != nullptr) {
            m_logManager->debug(QStringLiteral("检测线程"), QStringLiteral("结果图绘制完成。"));
        }
        const QImage resultImage = utils::matToQImage(annotatedImage);
        if (resultImage.isNull()) {
            emit failed(QStringLiteral("检测结果图转换失败。"));
            return;
        }
        if (m_logManager != nullptr) {
            m_logManager->debug(
                QStringLiteral("检测线程"),
                QStringLiteral("结果图转换成功：%1x%2")
                    .arg(resultImage.width())
                    .arg(resultImage.height()));
        }

        if (m_cancelRequested.load()) {
            emit canceled();
            return;
        }

        if (m_logManager != nullptr) {
            m_logManager->debug(QStringLiteral("检测线程"), QStringLiteral("准备发送完成信号。"));
        }
        emit completed(result, resultImage);
    } catch (const cv::Exception &exception) {
        emit failed(QStringLiteral("检测线程 OpenCV 异常：%1").arg(QString::fromLocal8Bit(exception.what())));
    } catch (const std::exception &exception) {
        emit failed(QStringLiteral("检测线程异常：%1").arg(QString::fromLocal8Bit(exception.what())));
    } catch (...) {
        emit failed(QStringLiteral("检测线程发生未知异常。"));
    }
}
