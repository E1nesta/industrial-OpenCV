#include "common/utils.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <QDateTime>
#include <QDebug>

#include <opencv2/imgproc.hpp>

namespace utils
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
    try {
        if (mat.empty()) {
            return {};
        }

        // 仅处理当前主链支持的 8bit 1/3/4 通道类型，其他类型由上层兜底。
        switch (mat.type()) {
        case CV_8UC1: {
            QImage image(
                mat.data,
                mat.cols,
                mat.rows,
                static_cast<qsizetype>(mat.step),
                QImage::Format_Grayscale8);
            return image.copy();
        }
        case CV_8UC3: {
            QImage image(
                mat.data,
                mat.cols,
                mat.rows,
                static_cast<qsizetype>(mat.step),
                QImage::Format_BGR888);
            return image.copy();
        }
        case CV_8UC4: {
            QImage image(mat.data, mat.cols, mat.rows, static_cast<qsizetype>(mat.step), QImage::Format_ARGB32);
            return image.copy();
        }
        default:
            qWarning().noquote()
                << QStringLiteral("[utils] matToQImage unsupported type=%1 size=%2x%3 channels=%4")
                       .arg(mat.type())
                       .arg(mat.cols)
                       .arg(mat.rows)
                       .arg(mat.channels());
            return {};
        }
    } catch (const cv::Exception &exception) {
        qWarning().noquote()
            << QStringLiteral("[utils] matToQImage cv exception type=%1 size=%2x%3 step=%4 channels=%5 error=%6")
                   .arg(mat.type())
                   .arg(mat.cols)
                   .arg(mat.rows)
                   .arg(static_cast<qulonglong>(mat.step))
                   .arg(mat.channels())
                   .arg(QString::fromLocal8Bit(exception.what()));
        return {};
    } catch (const std::exception &exception) {
        qWarning().noquote()
            << QStringLiteral("[utils] matToQImage std exception type=%1 size=%2x%3 step=%4 channels=%5 error=%6")
                   .arg(mat.type())
                   .arg(mat.cols)
                   .arg(mat.rows)
                   .arg(static_cast<qulonglong>(mat.step))
                   .arg(mat.channels())
                   .arg(QString::fromLocal8Bit(exception.what()));
        return {};
    } catch (...) {
        qWarning().noquote()
            << QStringLiteral("[utils] matToQImage unknown exception type=%1 size=%2x%3 step=%4 channels=%5")
                   .arg(mat.type())
                   .arg(mat.cols)
                   .arg(mat.rows)
                   .arg(static_cast<qulonglong>(mat.step))
                   .arg(mat.channels());
        return {};
    }
}

QImage buildPreviewImage(const cv::Mat &mat, const VisionParam &param, int maxPreviewLongEdge)
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
    const QRect previewRoi = scaledRoi(param.roi, scale);
    if (previewRoi.isValid() && !previewRoi.isEmpty()) {
        cv::rectangle(
            preview,
            cv::Rect(previewRoi.x(), previewRoi.y(), previewRoi.width(), previewRoi.height()),
            cv::Scalar(0, 215, 255),
            2);
    }

    return matToQImage(preview);
}

cv::Mat qImageToMat(const QImage &image)
{
    if (image.isNull()) {
        return {};
    }

    // 统一先转 RGBA8888，再手工映射到 BGR，避免依赖不稳定颜色转换路径。
    const QImage converted = image.convertToFormat(QImage::Format_RGBA8888);
    cv::Mat bgr(converted.height(), converted.width(), CV_8UC3);

    for (int row = 0; row < converted.height(); ++row) {
        const uchar *src = converted.constScanLine(row);
        uchar *dst = bgr.ptr<uchar>(row);
        for (int col = 0; col < converted.width(); ++col) {
            const int srcOffset = col * 4;
            const int dstOffset = col * 3;
            dst[dstOffset] = src[srcOffset + 2];
            dst[dstOffset + 1] = src[srcOffset + 1];
            dst[dstOffset + 2] = src[srcOffset];
        }
    }

    return bgr;
}

cv::Mat drawDetectionOverlay(const cv::Mat &image, const DetectResult &result)
{
    if (image.empty()) {
        return {};
    }

    cv::Mat annotated = image.clone();
    // 按筛选后 defectRects 叠加缺陷框。
    for (const auto &rect : result.defectRects) {
        cv::rectangle(
            annotated,
            cv::Rect(rect.x(), rect.y(), rect.width(), rect.height()),
            cv::Scalar(0, 0, 255),
            2);
    }

    // 左上角固定显示结论与摘要，便于演示和记录回看。
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
