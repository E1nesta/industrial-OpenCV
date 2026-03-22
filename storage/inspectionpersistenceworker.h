#pragma once

#include <QObject>
#include <QString>

#include "models/detectionoutput.h"
#include "models/persistenceresult.h"
#include "storage/recordmanager.h"

class InspectionPersistenceWorker : public QObject
{
    Q_OBJECT

public:
    explicit InspectionPersistenceWorker(QString databasePath = QString(), QObject *parent = nullptr);

public slots:
    void persist(const DetectionOutput &output);

signals:
    void persistenceCompleted(const PersistenceResult &result);

private:
    QString resolvedImageSaveDirectory(const DetectionOutput &output) const;
    InspectionRecord buildInspectionRecord(const DetectionOutput &output) const;
    bool archiveDetectionImages(
        const DetectionOutput &output,
        InspectionRecord &record,
        QString *errorMessage) const;

    RecordManager m_recordManager;
};
