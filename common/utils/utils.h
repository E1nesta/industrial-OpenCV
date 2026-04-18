// 通用工具：utils.h 提供跨模块复用的轻量工具函数。
// 本文件用于减少重复逻辑并保持通用行为一致。
#pragma once

#include <QImage>
#include <QRect>
#include <QString>

#include <opencv2/core/mat.hpp>

#include "domain/entities/inspectionresult.h"
#include "domain/entities/recipe.h"

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
QImage buildPreviewImage(const cv::Mat &mat, const Recipe &param, int maxPreviewLongEdge = 960);
cv::Mat qImageToMat(const QImage &image);

// 在原图上叠加检测框与摘要文案。
cv::Mat drawInspectionOverlay(const cv::Mat &image, const InspectionResult &result);
} // namespace utils
