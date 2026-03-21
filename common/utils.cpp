#include "common/utils.h"

#include <string>

#include <QDateTime>

#include <opencv2/imgproc.hpp>

namespace utils
{
QString boolToResultText(bool isOk)
{
    return isOk ? QStringLiteral("OK") : QStringLiteral("NG");
}

QString formatRoi(const QRect &roi)
{
    if (!roi.isValid() || roi.isEmpty()) {
        return QStringLiteral("未设置");
    }

    return QStringLiteral("x=%1, y=%2, w=%3, h=%4")
        .arg(roi.x())
        .arg(roi.y())
        .arg(roi.width())
        .arg(roi.height());
}

QString currentTimestamp()
{
    return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
}

QImage matToQImage(const cv::Mat &mat)
{
    if (mat.empty()) {
        return {};
    }

    switch (mat.type()) {
    case CV_8UC1: {
        QImage image(mat.data, mat.cols, mat.rows, static_cast<qsizetype>(mat.step), QImage::Format_Grayscale8);
        return image.copy();
    }
    case CV_8UC3: {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        QImage image(rgb.data, rgb.cols, rgb.rows, static_cast<qsizetype>(rgb.step), QImage::Format_RGB888);
        return image.copy();
    }
    case CV_8UC4: {
        QImage image(mat.data, mat.cols, mat.rows, static_cast<qsizetype>(mat.step), QImage::Format_ARGB32);
        return image.copy();
    }
    default:
        return {};
    }
}

cv::Mat drawDetectionOverlay(const cv::Mat &image, const DetectResult &result)
{
    if (image.empty()) {
        return {};
    }

    cv::Mat annotated = image.clone();
    for (const auto &rect : result.defectRects) {
        cv::rectangle(annotated, rect, cv::Scalar(0, 0, 255), 2);
    }

    const std::string resultText = boolToResultText(result.isOk).toStdString();
    const cv::Scalar resultColor = result.isOk ? cv::Scalar(0, 180, 0) : cv::Scalar(0, 0, 255);
    cv::putText(annotated, resultText, cv::Point(24, 42), cv::FONT_HERSHEY_SIMPLEX, 1.0, resultColor, 2);

    const std::string detailText = QStringLiteral("defects=%1 time=%2ms")
                                       .arg(result.defectCount)
                                       .arg(result.processTimeMs, 0, 'f', 2)
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
} // namespace utils
