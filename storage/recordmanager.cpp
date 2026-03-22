#include "storage/recordmanager.h"

#include <utility>

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QMutexLocker>

RecordManager::RecordManager(QString databasePath)
    : m_databaseManager(std::move(databasePath))
{
}

bool RecordManager::initialize(QString *errorMessage) const
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    QMutexLocker locker(&m_initializeMutex);
    if (m_isInitialized) {
        return true;
    }

    const bool ok = m_databaseManager.initialize(errorMessage);
    if (ok) {
        m_isInitialized = true;
    }

    return ok;
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
                "(inspection_id, capture_id, timestamp, batch_no, source_type, source_path, source_name, frame_index, "
                "is_ok, defect_count, process_time_ms, image_path, result_image_path) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
            query.addBindValue(record.inspectionId);
            query.addBindValue(record.captureId);
            query.addBindValue(record.timestamp);
            query.addBindValue(record.batchNo);
            query.addBindValue(inputSourceTypeToString(record.sourceType));
            query.addBindValue(record.sourcePath);
            query.addBindValue(record.sourceName);
            query.addBindValue(record.frameIndex);
            query.addBindValue(record.isOk ? 1 : 0);
            query.addBindValue(record.defectCount);
            query.addBindValue(record.processTimeMs);
            query.addBindValue(record.imagePath);
            query.addBindValue(record.resultImagePath);

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

bool RecordManager::lookupRecordByInspectionId(
    const QString &inspectionId,
    InspectionRecord *record,
    QString *errorMessage) const
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (record != nullptr) {
        *record = InspectionRecord{};
    }

    if (record == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("输出记录指针为空。");
        }
        return false;
    }

    if (inspectionId.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("检测编号为空。");
        }
        return false;
    }

    if (!initialize(errorMessage)) {
        return false;
    }

    const QString connectionName = m_databaseManager.createConnectionName(QStringLiteral("lookup"));
    bool found = false;

    {
        QSqlDatabase database = m_databaseManager.openConnection(connectionName, errorMessage);
        if (database.isOpen()) {
            QSqlQuery query(database);
            query.prepare(QStringLiteral(
                "SELECT inspection_id, capture_id, timestamp, batch_no, source_type, source_path, source_name, frame_index, "
                "is_ok, defect_count, process_time_ms, image_path, result_image_path "
                "FROM inspection_records "
                "WHERE inspection_id = ? "
                "ORDER BY id DESC LIMIT 1"));
            query.addBindValue(inspectionId);

            if (query.exec()) {
                if (query.next()) {
                    record->inspectionId = query.value(0).toString();
                    record->captureId = query.value(1).toString();
                    record->timestamp = query.value(2).toString();
                    record->batchNo = query.value(3).toString();
                    record->sourceType = inputSourceTypeFromString(query.value(4).toString());
                    record->sourcePath = query.value(5).toString();
                    record->sourceName = query.value(6).toString();
                    record->frameIndex = query.value(7).toLongLong();
                    record->isOk = query.value(8).toInt() != 0;
                    record->defectCount = query.value(9).toInt();
                    record->processTimeMs = query.value(10).toDouble();
                    record->imagePath = query.value(11).toString();
                    record->resultImagePath = query.value(12).toString();
                    found = true;
                }
            } else if (errorMessage != nullptr) {
                *errorMessage = query.lastError().text();
            }

            database.close();
        }
    }

    QSqlDatabase::removeDatabase(connectionName);
    return found;
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
                "SELECT inspection_id, capture_id, timestamp, batch_no, source_type, source_path, source_name, frame_index, "
                "is_ok, defect_count, process_time_ms, image_path, result_image_path "
                "FROM inspection_records "
                "ORDER BY id DESC LIMIT ?"));
            query.addBindValue(limit);

            if (query.exec()) {
                while (query.next()) {
                    InspectionRecord record;
                    record.inspectionId = query.value(0).toString();
                    record.captureId = query.value(1).toString();
                    record.timestamp = query.value(2).toString();
                    record.batchNo = query.value(3).toString();
                    record.sourceType = inputSourceTypeFromString(query.value(4).toString());
                    record.sourcePath = query.value(5).toString();
                    record.sourceName = query.value(6).toString();
                    record.frameIndex = query.value(7).toLongLong();
                    record.isOk = query.value(8).toInt() != 0;
                    record.defectCount = query.value(9).toInt();
                    record.processTimeMs = query.value(10).toDouble();
                    record.imagePath = query.value(11).toString();
                    record.resultImagePath = query.value(12).toString();
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
