// 通用工具：utils.cpp 提供跨模块复用的轻量工具函数。
// 本文件用于减少重复逻辑并保持通用行为一致。
#include "common/utils/utils.h"

#include <exception>

#include <QDateTime>
#include <QDebug>

#include <opencv2/core.hpp>

namespace utils
{
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
            QImage image(
                mat.data,
                mat.cols,
                mat.rows,
                static_cast<qsizetype>(mat.step),
                QImage::Format_ARGB32);
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
} // namespace utils
