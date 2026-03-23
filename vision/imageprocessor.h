#pragma once

#include <functional>

#include <opencv2/core/mat.hpp>

#include "models/detectresult.h"
#include "models/visionparam.h"

class LogManager;

// ImageProcessor 专注于图像处理与缺陷筛选，不负责 UI/存储/通信等外围流程。
class ImageProcessor
{
public:
    // 检测链入口：
    // ROI -> 灰度化 -> 二值化 -> 可选形态学 -> 轮廓提取 -> 面积筛选。
    DetectResult process(
        const cv::Mat &image,
        const VisionParam &param,
        LogManager *logManager = nullptr,
        const std::function<bool()> &shouldCancel = {}) const;
};
