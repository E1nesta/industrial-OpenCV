#pragma once

#include <QMetaType>
#include <QRect>
#include <QString>
#include <QVector>

#include "domain/entities/capturedframe.h"

struct InspectionResult
{
    QString inspectionId;
    FrameMeta frameMeta;
    bool isOk = true;
    bool canceled = false;
    int defectCount = 0;
    double processTimeMs = 0.0;
    QString message;
    QVector<QRect> defectRects;
};

Q_DECLARE_METATYPE(InspectionResult)
