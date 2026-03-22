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

    const QImage converted = image.convertToFormat(QImage::Format_RGBA8888);
    cv::Mat rgba(
        converted.height(),
        converted.width(),
        CV_8UC4,
        const_cast<uchar *>(converted.constBits()),
        static_cast<size_t>(converted.bytesPerLine()));

    cv::Mat bgr;
    cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
    return bgr.clone();
}

cv::Mat drawDetectionOverlay(const cv::Mat &image, const DetectResult &result)
{
    if (image.empty()) {
        return {};
    }

    cv::Mat annotated = image.clone();
    for (const auto &rect : result.defectRects) {
        cv::rectangle(
            annotated,
            cv::Rect(rect.x(), rect.y(), rect.width(), rect.height()),
            cv::Scalar(0, 0, 255),
            2);
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
