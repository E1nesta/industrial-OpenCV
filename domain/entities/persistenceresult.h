#pragma once

#include <QMetaType>
#include <QString>

#include "domain/entities/inspectionrecord.h"

struct PersistenceResult
{
    QString inspectionId;
    QString captureId;
    bool archiveSucceeded = false;
    bool recordSaved = false;
    QString archivedSourcePath;
    QString resultImagePath;
    InspectionRecord record;
    QString archiveMessage;
    QString recordError;
};

Q_DECLARE_METATYPE(PersistenceResult)
