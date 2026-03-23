#pragma once

#include <QImage>
#include <QRect>
#include <QString>

#include <opencv2/core/mat.hpp>

#include "models/detectresult.h"
#include "models/visionparam.h"

namespace utils
{
// 布尔检测结果转文本标签。
QString boolToResultText(bool isOk);
// ROI 统一格式化为界面可读字符串。
QString formatRoi(const QRect &roi);
// 当前本地时间戳，供记录与日志使用。
QString currentTimestamp();

// OpenCV Mat 与 QImage 的互转入口。
QImage matToQImage(const cv::Mat &mat);
QImage buildPreviewImage(const cv::Mat &mat, const VisionParam &param, int maxPreviewLongEdge = 960);
cv::Mat qImageToMat(const QImage &image);

// 在原图上叠加检测框与摘要文案。
cv::Mat drawDetectionOverlay(const cv::Mat &image, const DetectResult &result);
} // namespace utils
