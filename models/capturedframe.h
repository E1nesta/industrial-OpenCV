#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

#include <opencv2/core/mat.hpp>

#include "models/inputsource.h"

struct FrameMeta
{
    QString captureId;
    InputSourceType sourceType = InputSourceType::FileImage;
    QString sourcePath;
    QString sourceName;
    qint64 frameIndex = -1;
    QDateTime capturedAt;
};

struct CapturedFrame
{
    FrameMeta meta;
    cv::Mat image;

    bool isValid() const
    {
        return !image.empty();
    }
};

Q_DECLARE_METATYPE(FrameMeta)
Q_DECLARE_METATYPE(CapturedFrame)
