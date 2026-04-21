// 基础设施视觉：inspectionoverlayrenderer 负责生成巡检结果叠加图。
// 本文件位于执行链末端，把结构化结果转换为可展示、可归档的结果图。
#pragma once

#include <opencv2/core/mat.hpp>

#include "domain/entities/inspectionresult.h"

namespace inspectionoverlayrenderer
{
// 结果图生成入口：在原图上叠加缺陷框与检测摘要。
cv::Mat drawInspectionOverlay(const cv::Mat &image, const InspectionResult &result);
} // namespace inspectionoverlayrenderer
