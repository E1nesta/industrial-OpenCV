#pragma once

#include <QMetaType>

#include <opencv2/core/mat.hpp>

#include "domain/entities/inspectionresult.h"
#include "domain/entities/inspectiontask.h"

struct InspectionOutput
{
    InspectionTask request;
    InspectionResult result;
    cv::Mat annotatedImage;
};

Q_DECLARE_METATYPE(InspectionOutput)
