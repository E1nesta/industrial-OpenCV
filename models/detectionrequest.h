#pragma once

#include <QMetaType>
#include <QString>

#include "models/capturedframe.h"
#include "models/visionparam.h"

struct DetectionRequest
{
    QString inspectionId;
    CapturedFrame frame;
    VisionParam visionParam;
};

Q_DECLARE_METATYPE(DetectionRequest)
