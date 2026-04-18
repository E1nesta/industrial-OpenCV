#pragma once

#include <functional>

#include <QImage>
#include <QString>

#include "domain/entities/deviceconfig.h"
#include "domain/entities/inspectionoutput.h"

class LogManager;

struct ResultDispatchOutcome
{
    InspectionResult result;
    QImage resultImage;
    QString statusMessage;
};

class ResultDispatcher
{
public:
    ResultDispatchOutcome dispatch(
        const InspectionOutput &output,
        LogManager *logManager,
        const std::function<void(const InspectionOutput &)> &persistenceSink,
        const std::function<void(const QString &, bool, const DeviceConfig &)> &tcpSink) const;
};
