#pragma once

#include <QMetaType>
#include <QRect>

struct VisionParam
{
    int threshold = 128;
    int minArea = 10;
    int maxArea = 1000;
    QRect roi;
    bool enableMorphology = false;
};

Q_DECLARE_METATYPE(VisionParam)
