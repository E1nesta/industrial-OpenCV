#pragma once

#include <QMetaType>
#include <QRect>
#include <QString>

struct VisionParam
{
    int threshold = 128;
    int minArea = 10;
    int maxArea = 1000;
    QRect roi;
    bool enableMorphology = false;
    QString imageSavePath = QStringLiteral("data/images");
};

Q_DECLARE_METATYPE(VisionParam)
