#pragma once

#include <QMetaType>

#include <opencv2/core/mat.hpp>

#include "models/detectresult.h"
#include "models/detectionrequest.h"

struct DetectionOutput
{
    DetectionRequest request;
    DetectResult result;
    cv::Mat annotatedImage;
};

Q_DECLARE_METATYPE(DetectionOutput)
