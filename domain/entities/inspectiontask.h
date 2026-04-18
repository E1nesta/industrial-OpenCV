#pragma once

#include <QMetaType>
#include <QString>

#include "domain/entities/capturedframe.h"
#include "domain/entities/deviceconfig.h"
#include "domain/entities/recipe.h"

struct InspectionTask
{
    QString inspectionId;
    CapturedFrame frame;
    Recipe recipe;
    bool shouldSendTcpResult = false;
    DeviceConfig tcpDeviceConfig;
};

Q_DECLARE_METATYPE(InspectionTask)
