// 领域服务：imageprocessor.cpp 负责核心图像处理与业务计算。
// 本文件处于巡检核心处理阶段，输出结构化业务结果。
#include "domain/services/imageprocessor.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>

#include <QtGlobal>

#include <opencv2/imgproc.hpp>

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

GrayConversionMode resolveGrayConversionMode(const Recipe &param)
{
    // 灰度化策略遵循参数配置；OpenCvCvtColor 仅在显式放行时启用。
    if (param.grayConversionMode != GrayConversionMode::OpenCvCvtColor) {
        return param.grayConversionMode;
    }

    if (allowExperimentalOpenCvGrayMode()) {
        return GrayConversionMode::OpenCvCvtColor;
    }

    // 领域算法默认选择稳定路径，不直接承担项目级日志输出职责。
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

GrayConversionMode ImageProcessor::effectiveGrayConversionMode(const Recipe &param)
{
    return resolveGrayConversionMode(param);
}

InspectionResult ImageProcessor::process(const cv::Mat &image, const Recipe &param) const
{
    // 检测算法主流程：
    // 输入校验 -> ROI 裁剪 -> 灰度化 -> 二值化/形态学 -> 轮廓筛选 -> 结果汇总。
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();

    InspectionResult result;

    // 入口校验：空图直接失败，避免后续 OpenCV 调用触发无效输入路径。
    if (image.empty()) {
        result.isOk = false;
        result.failureReason = QStringLiteral("输入图像为空。");
        result.summaryText = result.failureReason;
        return result;
    }

    const int effectiveThreshold = normalizedThreshold(param);
    const double effectiveMinArea = normalizedMinArea(param);
    const double effectiveMaxArea = normalizedMaxArea(param, effectiveMinArea);

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
    }

    if (!param.enableDefectDetection) {
        result.isOk = true;
        result.defectCount = 0;
        result.summaryText = QStringLiteral("AOI 缺陷检测项已关闭，本次按 OK 收敛。");
        const auto end = clock::now();
        result.elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }

    // 步骤 2 - 灰度化：统一转换为单通道，为阈值与轮廓处理提供稳定输入。
    const GrayConversionMode effectiveGrayMode = effectiveGrayConversionMode(param);
    cv::Mat gray = convertToGray(workingImage, effectiveGrayMode);

    // 步骤 3 - 二值化/形态学：提取前景并可选去噪，提升轮廓稳定性。
    cv::Mat binary;
    cv::threshold(gray, binary, effectiveThreshold, 255, cv::THRESH_BINARY_INV);

    if (param.enableMorphology) {
        const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
        cv::morphologyEx(binary, binary, cv::MORPH_OPEN, kernel);
    }

    // 步骤 4 - 轮廓筛选：按面积过滤候选轮廓并输出缺陷框。
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    result.defects.clear();
    for (const auto &contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < effectiveMinArea || area > effectiveMaxArea) {
            continue;
        }

        cv::Rect rect = cv::boundingRect(contour);
        rect.x += roiRect.x;
        rect.y += roiRect.y;

        DefectItem defect;
        defect.boundingRect = QRect(rect.x, rect.y, rect.width, rect.height);
        defect.area = area;
        defect.category = QStringLiteral("blob_defect");
        defect.description = QStringLiteral("AOI 外观缺陷候选区域");
        result.defects.append(defect);
    }

    result.defectCount = static_cast<int>(result.defects.size());
    result.isOk = result.defectCount == 0;
    result.summaryText = result.isOk
                             ? QStringLiteral("AOI 外观检测通过。")
                             : QStringLiteral("AOI 外观检测 NG：检测到 %1 处缺陷。").arg(result.defectCount);

    // 步骤 5 - 结果收尾：统计耗时并输出统一结果结构。
    const auto end = clock::now();
    result.elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();
    return result;
}
