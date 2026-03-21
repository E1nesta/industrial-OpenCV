#include "vision/detectionworker.h"

#include <QImage>

#include <opencv2/core/mat.hpp>

#include "camera/cameramanager.h"
#include "common/utils.h"
#include "vision/imageprocessor.h"

DetectionWorker::DetectionWorker(QObject *parent)
    : QObject(parent)
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

void DetectionWorker::process(const QString &imagePath, const VisionParam &param)
{
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

    const cv::Mat sourceImage = cameraManager.grabImage();
    if (sourceImage.empty()) {
        emit failed(QStringLiteral("图片载入成功但图像为空：%1").arg(imagePath));
        return;
    }

    ImageProcessor imageProcessor;
    DetectResult result =
        imageProcessor.process(sourceImage, param, [this]() { return m_cancelRequested.load(); });
    result.imagePath = imagePath;

    if (result.canceled || m_cancelRequested.load()) {
        emit canceled();
        return;
    }

    const cv::Mat annotatedImage = utils::drawDetectionOverlay(sourceImage, result);
    const QImage resultImage = utils::matToQImage(annotatedImage);
    if (resultImage.isNull()) {
        emit failed(QStringLiteral("检测结果图转换失败。"));
        return;
    }

    if (m_cancelRequested.load()) {
        emit canceled();
        return;
    }

    emit completed(result, resultImage);
}
