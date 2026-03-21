#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include "models/detectresult.h"
#include "models/inspectionrecord.h"
#include "models/visionparam.h"
#include "storage/recordmanager.h"

class InspectionPersistenceWorker : public QObject
{
    Q_OBJECT

public:
    explicit InspectionPersistenceWorker(QString databasePath = QString(), QObject *parent = nullptr);

public slots:
    void persist(const DetectResult &result, const QImage &resultImage, const VisionParam &param);

signals:
    void persistenceCompleted(
        const InspectionRecord &record,
        bool archiveSucceeded,
        const QString &archiveMessage,
        bool recordSaved,
        const QString &recordError);

private:
    QString resolvedImageSaveDirectory(const VisionParam &param) const;
    InspectionRecord buildInspectionRecord(const DetectResult &result) const;
    bool archiveDetectionImages(
        const DetectResult &result,
        const QImage &resultImage,
        const VisionParam &param,
        InspectionRecord &record,
        QString *errorMessage) const;

    RecordManager m_recordManager;
};
