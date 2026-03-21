#pragma once

#include <functional>

#include <opencv2/core/mat.hpp>

#include "models/detectresult.h"
#include "models/visionparam.h"

class LogManager;

class ImageProcessor
{
public:
    DetectResult process(
        const cv::Mat &image,
        const VisionParam &param,
        LogManager *logManager = nullptr,
        const std::function<bool()> &shouldCancel = {}) const;
};
