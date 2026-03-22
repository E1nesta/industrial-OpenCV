#pragma once

#include <QMetaType>
#include <QString>

#include "models/inputsource.h"

struct InspectionRecord
{
    QString inspectionId;
    QString captureId;
    QString timestamp;
    QString batchNo;
    InputSourceType sourceType = InputSourceType::FileImage;
    QString sourcePath;
    QString sourceName;
    qint64 frameIndex = -1;
    bool isOk = true;
    int defectCount = 0;
    double processTimeMs = 0.0;
    QString imagePath;
    QString resultImagePath;
};

Q_DECLARE_METATYPE(InspectionRecord)
