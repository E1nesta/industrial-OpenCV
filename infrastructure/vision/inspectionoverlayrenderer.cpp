// 基础设施视觉：inspectionoverlayrenderer 负责生成巡检结果叠加图。
// 本文件位于执行链末端，把结构化结果转换为可展示、可归档的结果图。
#include "infrastructure/vision/inspectionoverlayrenderer.h"

#include <string>

#include <opencv2/imgproc.hpp>

namespace inspectionoverlayrenderer
{
namespace
{
QString resultText(bool isOk)
{
    return isOk ? QStringLiteral("OK") : QStringLiteral("NG");
}
} // namespace

cv::Mat drawInspectionOverlay(const cv::Mat &image, const InspectionResult &result)
{
    if (image.empty()) {
        return {};
    }

    cv::Mat annotated = image.clone();

    // 先叠加缺陷框，让结果图保持与结构化结果一致。
    for (const DefectItem &defect : result.defects) {
        const QRect &rect = defect.boundingRect;
        cv::rectangle(
            annotated,
            cv::Rect(rect.x(), rect.y(), rect.width(), rect.height()),
            cv::Scalar(0, 0, 255),
            2);
    }

    // 左上角固定显示结论与摘要，便于演示和记录回看。
    const std::string overlayResultText = resultText(result.isOk).toStdString();
    const cv::Scalar resultColor = result.isOk ? cv::Scalar(0, 180, 0) : cv::Scalar(0, 0, 255);
    cv::putText(
        annotated,
        overlayResultText,
        cv::Point(24, 42),
        cv::FONT_HERSHEY_SIMPLEX,
        1.0,
        resultColor,
        2);

    const std::string detailText = QStringLiteral("defects=%1 time=%2ms")
                                       .arg(result.defectCount)
                                       .arg(result.elapsedMs, 0, 'f', 2)
                                       .toStdString();
    cv::putText(
        annotated,
        detailText,
        cv::Point(24, 78),
        cv::FONT_HERSHEY_SIMPLEX,
        0.7,
        cv::Scalar(255, 200, 0),
        2);

    return annotated;
}
} // namespace inspectionoverlayrenderer
