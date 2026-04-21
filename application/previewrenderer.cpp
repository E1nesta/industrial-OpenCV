// 应用层渲染：previewrenderer 负责把输入帧整理为界面预览图。
// 本文件只处理预览显示，不承担检测结论绘制。
#include "application/previewrenderer.h"

#include <algorithm>
#include <cmath>

#include <QRect>

#include <opencv2/imgproc.hpp>

#include "common/utils/utils.h"

namespace previewrenderer
{
namespace
{
QRect scaledRoi(const QRect &roi, double scale)
{
    if (!roi.isValid() || roi.isEmpty()) {
        return {};
    }

    return QRect(
        static_cast<int>(std::lround(roi.x() * scale)),
        static_cast<int>(std::lround(roi.y() * scale)),
        static_cast<int>(std::lround(roi.width() * scale)),
        static_cast<int>(std::lround(roi.height() * scale)));
}
} // namespace

QImage buildPreviewImage(const cv::Mat &mat, const Recipe &recipe, int maxPreviewLongEdge)
{
    if (mat.empty()) {
        return {};
    }

    // 预览链优先限制长边，控制 UI 绘制负载。
    const int longEdge = std::max(mat.cols, mat.rows);
    const double scale = (maxPreviewLongEdge > 0 && longEdge > maxPreviewLongEdge)
                             ? static_cast<double>(maxPreviewLongEdge) / static_cast<double>(longEdge)
                             : 1.0;

    cv::Mat preview;
    if (scale < 1.0) {
        cv::resize(mat, preview, cv::Size(), scale, scale, cv::INTER_AREA);
    } else {
        preview = mat.clone();
    }

    // 预览图只叠加 ROI 框，不引入检测链逻辑。
    const QRect previewRoi = scaledRoi(recipe.roi, scale);
    if (previewRoi.isValid() && !previewRoi.isEmpty()) {
        cv::rectangle(
            preview,
            cv::Rect(previewRoi.x(), previewRoi.y(), previewRoi.width(), previewRoi.height()),
            cv::Scalar(0, 215, 255),
            2);
    }

    return utils::matToQImage(preview);
}
} // namespace previewrenderer
