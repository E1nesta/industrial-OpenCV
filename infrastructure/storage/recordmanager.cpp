// 基础设施存储：recordmanager.cpp 负责记录落库与图片归档。
// 本文件承接巡检结果留痕出口，处理持久化副作用。
#include "infrastructure/storage/recordmanager.h"

#include <utility>

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QMutexLocker>

RecordManager::RecordManager(QString databasePath)
    : m_databaseManager(std::move(databasePath))
{
    // 底层数据库路径在构造时注入，后续连接均复用该配置。
}

bool RecordManager::initialize(QString *errorMessage) const
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    // 初始化只需成功一次，后续调用快速返回，避免重复建表。
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

    // 每次写入使用独立连接名，符合 Qt SQL 的线程使用约束。
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
    // 回查入口先统一做参数校验，避免无效查询污染错误信息。
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

    // 按 inspectionId 回查最近一条记录，支持结果回看场景。
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
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    if (limit <= 0) {
        return records;
    }

    if (!initialize(errorMessage)) {
        return records;
    }

    // 最近记录用于界面列表展示，按自增 id 逆序读取。
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
    // 直接复用 DatabaseManager 的最终路径解析结果。
    return m_databaseManager.databaseFilePath();
}
