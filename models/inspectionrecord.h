#pragma once

#include <QMetaType>
#include <QString>

struct InspectionRecord
{
    QString inspectionId;
    QString timestamp;
    QString batchNo;
    bool isOk = true;
    int defectCount = 0;
    double processTimeMs = 0.0;
    QString imagePath;
    QString resultImagePath;
};

Q_DECLARE_METATYPE(InspectionRecord)
