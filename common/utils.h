#pragma once

#include <QImage>
#include <QRect>
#include <QString>

#include <opencv2/core/mat.hpp>

#include "models/detectresult.h"
#include "models/visionparam.h"

namespace utils
{
QString boolToResultText(bool isOk);
QString formatRoi(const QRect &roi);
QString currentTimestamp();
QImage matToQImage(const cv::Mat &mat);
QImage buildPreviewImage(const cv::Mat &mat, const VisionParam &param, int maxPreviewLongEdge = 960);
cv::Mat qImageToMat(const QImage &image);
cv::Mat drawDetectionOverlay(const cv::Mat &image, const DetectResult &result);
} // namespace utils
