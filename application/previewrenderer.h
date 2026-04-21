// 应用层渲染：previewrenderer 负责把输入帧整理为界面预览图。
// 本文件只处理预览显示，不承担检测结论绘制。
#pragma once

#include <QImage>

#include <opencv2/core/mat.hpp>

#include "domain/entities/recipe.h"

namespace previewrenderer
{
// 预览图生成入口：按长边限制缩放，并叠加当前配方 ROI。
QImage buildPreviewImage(const cv::Mat &mat, const Recipe &recipe, int maxPreviewLongEdge = 960);
} // namespace previewrenderer
