// 领域服务：imageprocessor.h 负责核心图像处理与业务计算。
// 本文件处于巡检核心处理阶段，输出结构化业务结果。
#pragma once

#include <opencv2/core/mat.hpp>

#include "domain/entities/inspectionresult.h"
#include "domain/entities/recipe.h"

// ImageProcessor 专注于图像处理与缺陷筛选，不负责 UI/存储/通信等外围流程。
class ImageProcessor
{
public:
    // 计算当前配方在运行时真正生效的灰度模式。
    static GrayConversionMode effectiveGrayConversionMode(const Recipe &param);

    // 检测链入口：
    // ROI -> 灰度化 -> 二值化 -> 可选形态学 -> 轮廓提取 -> 面积筛选。
    InspectionResult process(const cv::Mat &image, const Recipe &param) const;
};
