#include "vision/imageprocessor.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>

#include <opencv2/imgproc.hpp>

#include "logger/logmanager.h"

DetectResult ImageProcessor::process(
    const cv::Mat &image,
    const VisionParam &param,
    LogManager *logManager,
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
    const auto logDebug = [logManager](const QString &message) {
        if (logManager != nullptr) {
            logManager->debug(QStringLiteral("图像处理"), message);
        }
    };

    if (image.empty()) {
        result.isOk = false;
        result.message = QStringLiteral("输入图像为空。");
        return result;
    }

    logDebug(QStringLiteral("开始：%1x%2 channels=%3 threshold=%4 minArea=%5 maxArea=%6 morphology=%7")
                 .arg(image.cols)
                 .arg(image.rows)
                 .arg(image.channels())
                 .arg(param.threshold)
                 .arg(param.minArea)
                 .arg(param.maxArea)
                 .arg(param.enableMorphology ? QStringLiteral("true") : QStringLiteral("false")));

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
        logDebug(QStringLiteral("ROI 生效：x=%1 y=%2 w=%3 h=%4")
                     .arg(roiRect.x)
                     .arg(roiRect.y)
                     .arg(roiRect.width)
                     .arg(roiRect.height));
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
    logDebug(QStringLiteral("灰度化完成。"));

    if (isCanceled()) {
        return makeCanceledResult();
    }

    cv::Mat binary;
    cv::threshold(gray, binary, param.threshold, 255, cv::THRESH_BINARY_INV);
    logDebug(QStringLiteral("二值化完成。"));

    if (param.enableMorphology) {
        const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
        cv::morphologyEx(binary, binary, cv::MORPH_OPEN, kernel);
        logDebug(QStringLiteral("形态学处理完成。"));
    }

    if (isCanceled()) {
        return makeCanceledResult();
    }

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    logDebug(QStringLiteral("轮廓提取完成：count=%1").arg(contours.size()));

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
    logDebug(QStringLiteral("缺陷筛选完成：kept=%1").arg(result.defectRects.size()));

    result.defectCount = static_cast<int>(result.defectRects.size());
    result.isOk = result.defectCount == 0;
    result.message = result.isOk
                         ? QStringLiteral("检测通过。")
                         : QStringLiteral("检测到 %1 处缺陷。").arg(result.defectCount);

    const auto end = clock::now();
    result.processTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
    logDebug(QStringLiteral("完成：result=%1 defects=%2 time=%3 ms")
                 .arg(result.isOk ? QStringLiteral("OK") : QStringLiteral("NG"))
                 .arg(result.defectCount)
                 .arg(result.processTimeMs, 0, 'f', 2));
    return result;
}
