#include "vision/imageprocessor.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>

#include <opencv2/imgproc.hpp>

DetectResult ImageProcessor::process(
    const cv::Mat &image,
    const VisionParam &param,
    const std::function<bool()> &shouldCancel) const
{
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();

    DetectResult result;
    const auto makeCanceledResult = [&result]() {
        result.canceled = true;
        result.isOk = false;
        result.message = QStringLiteral("检测已取消。");
        return result;
    };

    const auto isCanceled = [&shouldCancel]() {
        return static_cast<bool>(shouldCancel) && shouldCancel();
    };

    if (image.empty()) {
        result.isOk = false;
        result.message = QStringLiteral("输入图像为空。");
        return result;
    }

    if (isCanceled()) {
        return makeCanceledResult();
    }

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

    if (isCanceled()) {
        return makeCanceledResult();
    }

    cv::Mat gray;
    if (workingImage.channels() == 1) {
        gray = workingImage;
    } else if (workingImage.channels() == 4) {
        cv::cvtColor(workingImage, gray, cv::COLOR_BGRA2GRAY);
    } else {
        cv::cvtColor(workingImage, gray, cv::COLOR_BGR2GRAY);
    }

    if (isCanceled()) {
        return makeCanceledResult();
    }

    cv::Mat binary;
    cv::threshold(gray, binary, param.threshold, 255, cv::THRESH_BINARY_INV);

    if (param.enableMorphology) {
        const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
        cv::morphologyEx(binary, binary, cv::MORPH_OPEN, kernel);
    }

    if (isCanceled()) {
        return makeCanceledResult();
    }

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    const double minArea = std::max(0, param.minArea);
    const double maxArea =
        param.maxArea > 0 ? static_cast<double>(param.maxArea) : std::numeric_limits<double>::max();

    result.defectRects.clear();
    for (const auto &contour : contours) {
        if (isCanceled()) {
            return makeCanceledResult();
        }

        const double area = cv::contourArea(contour);
        if (area < minArea || area > maxArea) {
            continue;
        }

        cv::Rect rect = cv::boundingRect(contour);
        rect.x += roiRect.x;
        rect.y += roiRect.y;
        result.defectRects.push_back(rect);
    }

    result.defectCount = static_cast<int>(result.defectRects.size());
    result.isOk = result.defectCount == 0;
    result.message = result.isOk
                         ? QStringLiteral("检测通过。")
                         : QStringLiteral("检测到 %1 处缺陷。").arg(result.defectCount);

    const auto end = clock::now();
    result.processTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
    return result;
}
