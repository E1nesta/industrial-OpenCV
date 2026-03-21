#pragma once

#include <QMetaType>
#include <QString>

#include <opencv2/core/types.hpp>

#include <vector>

struct DetectResult
{
    bool isOk = true;
    bool canceled = false;
    int defectCount = 0;
    double processTimeMs = 0.0;
    QString imagePath;
    QString message;
    std::vector<cv::Rect> defectRects;
};

Q_DECLARE_METATYPE(DetectResult)
