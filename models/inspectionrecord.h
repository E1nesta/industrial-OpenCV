#pragma once

#include <QString>

struct InspectionRecord
{
    QString timestamp;
    QString batchNo;
    bool isOk = true;
    int defectCount = 0;
    double processTimeMs = 0.0;
    QString imagePath;
};

