// 通用工具：utils.h 提供跨模块复用的轻量工具函数。
// 本文件用于减少重复逻辑并保持通用行为一致。
#pragma once

#include <QImage>
#include <QString>

#include <opencv2/core/mat.hpp>

namespace utils
{
// 当前本地时间戳，供记录与日志使用。
QString currentTimestamp();

// OpenCV Mat 与 QImage 的互转入口。
QImage matToQImage(const cv::Mat &mat);
cv::Mat qImageToMat(const QImage &image);
} // namespace utils
