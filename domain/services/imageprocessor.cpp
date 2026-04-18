// 领域服务：imageprocessor.cpp 负责核心图像处理与业务计算。
// 本文件处于巡检核心处理阶段，输出结构化业务结果。
#include "domain/services/imageprocessor.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
#include <stdexcept>

#include <QtGlobal>
#include <QDebug>

#include <opencv2/imgproc.hpp>

#include "common/logging/logmanager.h"

namespace
{
void validateGrayInput(const cv::Mat &image)
{
    if (image.empty()) {
        return;
    }

    if (image.depth() != CV_8U) {
        throw std::runtime_error("Unsupported image depth for grayscale conversion");
    }

    if (image.channels() != 1 && image.channels() != 3 && image.channels() != 4) {
        throw std::runtime_error("Unsupported channel count for grayscale conversion");
    }
}

bool allowExperimentalOpenCvGrayMode()
{
    bool numericOk = false;
    const int numericValue = qEnvironmentVariableIntValue("VISION_ALLOW_UNSTABLE_OPENCV_GRAY", &numericOk);
    if (numericOk) {
        return numericValue != 0;
    }

    const QByteArray value = qgetenv("VISION_ALLOW_UNSTABLE_OPENCV_GRAY").trimmed().toLower();
    return value == "true" || value == "yes" || value == "on";
}

GrayConversionMode resolveGrayConversionMode(const Recipe &param, LogManager *logManager)
{
    // 灰度化策略遵循参数配置；OpenCvCvtColor 仅在显式放行时启用。
    if (param.grayConversionMode != GrayConversionMode::OpenCvCvtColor) {
        return param.grayConversionMode;
    }

    if (allowExperimentalOpenCvGrayMode()) {
        return GrayConversionMode::OpenCvCvtColor;
    }

    if (logManager != nullptr) {
        logManager->warn(
            QStringLiteral("图像处理"),
            QStringLiteral(
                "OpenCvCvtColor 灰度模式属于实验路径，当前未显式放行，已自动回退到 stable_manual。"));
    } else {
        qWarning().noquote()
            << QStringLiteral(
                   "[imageprocessor] OpenCvCvtColor gray mode is gated and was downgraded to stable_manual.");
    }
    return GrayConversionMode::StableManual;
}

cv::Mat toGrayManualStable(const cv::Mat &image)
{
    if (image.empty()) {
        return {};
    }

    validateGrayInput(image);

    if (image.channels() == 1) {
        return image.clone();
    }

    cv::Mat gray(image.rows, image.cols, CV_8UC1);

    if (image.channels() == 3) {
        // 使用整数近似权重实现 BGR -> Gray，保持跨平台行为一致。
        for (int row = 0; row < image.rows; ++row) {
            const cv::Vec3b *src = image.ptr<cv::Vec3b>(row);
            uchar *dst = gray.ptr<uchar>(row);
            for (int col = 0; col < image.cols; ++col) {
                const cv::Vec3b &pixel = src[col];
                const int blue = pixel[0];
                const int green = pixel[1];
                const int red = pixel[2];
                dst[col] = static_cast<uchar>((red * 77 + green * 150 + blue * 29) >> 8);
            }
        }
        return gray;
    }

    if (image.channels() == 4) {
        // BGRA 路径忽略 alpha，只参与 B/G/R 加权。
        for (int row = 0; row < image.rows; ++row) {
            const cv::Vec4b *src = image.ptr<cv::Vec4b>(row);
            uchar *dst = gray.ptr<uchar>(row);
            for (int col = 0; col < image.cols; ++col) {
                const cv::Vec4b &pixel = src[col];
                const int blue = pixel[0];
                const int green = pixel[1];
                const int red = pixel[2];
                dst[col] = static_cast<uchar>((red * 77 + green * 150 + blue * 29) >> 8);
            }
        }
        return gray;
    }

    throw std::runtime_error("Unsupported channel count for grayscale conversion");
}

cv::Mat toGrayWithOpenCv(const cv::Mat &image)
{
    if (image.empty()) {
        return {};
    }

    validateGrayInput(image);

    if (image.channels() == 1) {
        return image.clone();
    }

    cv::Mat gray;
    if (image.channels() == 3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        return gray;
    }

    if (image.channels() == 4) {
        cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
        return gray;
    }

    throw std::runtime_error("Unsupported channel count for grayscale conversion");
}

cv::Mat convertToGray(const cv::Mat &image, GrayConversionMode mode)
{
    // 检测链统一在单通道图像上进行阈值与轮廓处理。
    switch (mode) {
    case GrayConversionMode::OpenCvCvtColor:
        return toGrayWithOpenCv(image);
    case GrayConversionMode::StableManual:
    default:
        return toGrayManualStable(image);
    }
}

int normalizedThreshold(const Recipe &param)
{
    return std::clamp(param.threshold, 0, 255);
}

double normalizedMinArea(const Recipe &param)
{
    return static_cast<double>(std::max(0, param.minArea));
}

double normalizedMaxArea(const Recipe &param, double minArea)
{
    if (param.maxArea <= 0) {
        return std::numeric_limits<double>::max();
    }

    return std::max(static_cast<double>(param.maxArea), minArea);
}
} // namespace

InspectionResult ImageProcessor::process(
    const cv::Mat &image,
    const Recipe &param,
    LogManager *logManager,
    const std::function<bool()> &shouldCancel) const
{
    // 检测算法主流程：
    // 输入校验 -> ROI 裁剪 -> 灰度化 -> 二值化/形态学 -> 轮廓筛选 -> 结果汇总。
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();

    InspectionResult result;
    const auto makeCanceledResult = [&result]() {
        result.canceled = true;
        result.isOk = false;
        result.message = QStringLiteral("检测已取消。");
        return result;
    };

    const auto isCanceled = [&shouldCancel]() {
        return static_cast<bool>(shouldCancel) && shouldCancel();
    };
    const auto logDebug = [logManager](const QString &message) {
        if (logManager != nullptr) {
            logManager->debug(QStringLiteral("图像处理"), message);
        }
    };

    // 入口校验：空图直接失败，避免后续 OpenCV 调用触发无效输入路径。
    if (image.empty()) {
        result.isOk = false;
        result.message = QStringLiteral("输入图像为空。");
        return result;
    }

    const int effectiveThreshold = normalizedThreshold(param);
    const double effectiveMinArea = normalizedMinArea(param);
    const double effectiveMaxArea = normalizedMaxArea(param, effectiveMinArea);

    logDebug(QStringLiteral("开始：%1x%2 channels=%3 threshold=%4 minArea=%5 maxArea=%6 morphology=%7")
                 .arg(image.cols)
                 .arg(image.rows)
                 .arg(image.channels())
                 .arg(effectiveThreshold)
                 .arg(effectiveMinArea)
                 .arg(effectiveMaxArea)
                 .arg(param.enableMorphology ? QStringLiteral("true") : QStringLiteral("false")));

    // 取消门控：在进入重计算步骤前优先响应取消请求。
    if (isCanceled()) {
        return makeCanceledResult();
    }

    // 步骤 1 - ROI 裁剪：在 ROI 范围内工作，降低后续处理负担并保持结果可映射回原图坐标。
    cv::Mat workingImage = image;
    cv::Rect roiRect;
    if (param.roi.isValid() && !param.roi.isEmpty()) {
        const int x = std::clamp(param.roi.x(), 0, image.cols - 1);
        const int y = std::clamp(param.roi.y(), 0, image.rows - 1);
        const int right = std::clamp(param.roi.x() + param.roi.width(), x + 1, image.cols);
        const int bottom = std::clamp(param.roi.y() + param.roi.height(), y + 1, image.rows);

        roiRect = cv::Rect(x, y, right - x, bottom - y);
        workingImage = image(roiRect).clone();
        logDebug(QStringLiteral("ROI 生效：x=%1 y=%2 w=%3 h=%4")
                     .arg(roiRect.x)
                     .arg(roiRect.y)
                     .arg(roiRect.width)
                     .arg(roiRect.height));
    }

    if (isCanceled()) {
        return makeCanceledResult();
    }

    // 步骤 2 - 灰度化：统一转换为单通道，为阈值与轮廓处理提供稳定输入。
    const GrayConversionMode effectiveGrayMode = resolveGrayConversionMode(param, logManager);
    cv::Mat gray = convertToGray(workingImage, effectiveGrayMode);
    logDebug(
        QStringLiteral("灰度化完成：requested=%1 effective=%2")
            .arg(grayConversionModeToString(param.grayConversionMode))
            .arg(grayConversionModeToString(effectiveGrayMode)));

    if (isCanceled()) {
        return makeCanceledResult();
    }

    // 步骤 3 - 二值化/形态学：提取前景并可选去噪，提升轮廓稳定性。
    cv::Mat binary;
    cv::threshold(gray, binary, effectiveThreshold, 255, cv::THRESH_BINARY_INV);
    logDebug(QStringLiteral("二值化完成。"));

    if (param.enableMorphology) {
        const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
        cv::morphologyEx(binary, binary, cv::MORPH_OPEN, kernel);
        logDebug(QStringLiteral("形态学处理完成。"));
    }

    if (isCanceled()) {
        return makeCanceledResult();
    }

    // 步骤 4 - 轮廓筛选：按面积过滤候选轮廓并输出缺陷框。
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    logDebug(QStringLiteral("轮廓提取完成：count=%1").arg(contours.size()));

    result.defectRects.clear();
    for (const auto &contour : contours) {
        if (isCanceled()) {
            return makeCanceledResult();
        }

        const double area = cv::contourArea(contour);
        if (area < effectiveMinArea || area > effectiveMaxArea) {
            continue;
        }

        cv::Rect rect = cv::boundingRect(contour);
        rect.x += roiRect.x;
        rect.y += roiRect.y;
        result.defectRects.append(QRect(rect.x, rect.y, rect.width, rect.height));
    }
    logDebug(QStringLiteral("缺陷筛选完成：kept=%1").arg(result.defectRects.size()));

    result.defectCount = static_cast<int>(result.defectRects.size());
    result.isOk = result.defectCount == 0;
    result.message = result.isOk
                         ? QStringLiteral("检测通过。")
                         : QStringLiteral("检测到 %1 处缺陷。").arg(result.defectCount);

    // 步骤 5 - 结果收尾：统计耗时并输出统一结果结构。
    const auto end = clock::now();
    result.processTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
    logDebug(QStringLiteral("完成：result=%1 defects=%2 time=%3 ms")
                 .arg(result.isOk ? QStringLiteral("OK") : QStringLiteral("NG"))
                 .arg(result.defectCount)
                 .arg(result.processTimeMs, 0, 'f', 2));
    return result;
}
