#include "storage/recordmanager.h"

#include <utility>

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

RecordManager::RecordManager(QString databasePath)
    : m_databaseManager(std::move(databasePath))
{
}

bool RecordManager::initialize(QString *errorMessage) const
{
    return m_databaseManager.initialize(errorMessage);
}

bool RecordManager::saveRecord(const InspectionRecord &record, QString *errorMessage) const
{
    if (!initialize(errorMessage)) {
        return false;
    }

    const QString connectionName = m_databaseManager.createConnectionName(QStringLiteral("save"));
    bool ok = false;

    {
        QSqlDatabase database = m_databaseManager.openConnection(connectionName, errorMessage);
        if (database.isOpen()) {
            QSqlQuery query(database);
            query.prepare(QStringLiteral(
                "INSERT INTO inspection_records "
                "(timestamp, batch_no, is_ok, defect_count, process_time_ms, image_path) "
                "VALUES (?, ?, ?, ?, ?, ?)"));
            query.addBindValue(record.timestamp);
            query.addBindValue(record.batchNo);
            query.addBindValue(record.isOk ? 1 : 0);
            query.addBindValue(record.defectCount);
            query.addBindValue(record.processTimeMs);
            query.addBindValue(record.imagePath);

            ok = query.exec();
            if (!ok && errorMessage != nullptr) {
                *errorMessage = query.lastError().text();
            }

            database.close();
        }
    }

    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

QList<InspectionRecord> RecordManager::recentRecords(int limit, QString *errorMessage) const
{
    QList<InspectionRecord> records;
    if (!initialize(errorMessage)) {
        return records;
    }

    const QString connectionName = m_databaseManager.createConnectionName(QStringLiteral("recent"));
    {
        QSqlDatabase database = m_databaseManager.openConnection(connectionName, errorMessage);
        if (database.isOpen()) {
            QSqlQuery query(database);
            query.prepare(QStringLiteral(
                "SELECT timestamp, batch_no, is_ok, defect_count, process_time_ms, image_path "
                "FROM inspection_records "
                "ORDER BY id DESC LIMIT ?"));
            query.addBindValue(limit);

            if (query.exec()) {
                while (query.next()) {
                    InspectionRecord record;
                    record.timestamp = query.value(0).toString();
                    record.batchNo = query.value(1).toString();
                    record.isOk = query.value(2).toInt() != 0;
                    record.defectCount = query.value(3).toInt();
                    record.processTimeMs = query.value(4).toDouble();
                    record.imagePath = query.value(5).toString();
                    records.append(record);
                }
            } else if (errorMessage != nullptr) {
                *errorMessage = query.lastError().text();
            }

            database.close();
        }
    }

    QSqlDatabase::removeDatabase(connectionName);
    return records;
}

QString RecordManager::databaseFilePath() const
{
    return m_databaseManager.databaseFilePath();
}
