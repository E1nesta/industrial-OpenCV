#pragma once

#include <QList>
#include <QString>

#include "models/inspectionrecord.h"
#include "storage/databasemanager.h"

class RecordManager
{
public:
    explicit RecordManager(QString databasePath = QString());

    bool initialize(QString *errorMessage = nullptr) const;
    bool saveRecord(const InspectionRecord &record, QString *errorMessage = nullptr) const;
    QList<InspectionRecord> recentRecords(int limit = 10, QString *errorMessage = nullptr) const;
    QString databaseFilePath() const;

private:
    DatabaseManager m_databaseManager;
};
