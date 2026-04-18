// 基础设施存储：recordmanager.h 负责记录落库与图片归档。
// 本文件承接巡检结果留痕出口，处理持久化副作用。
#pragma once

#include <QList>
#include <QMutex>
#include <QString>

#include "domain/entities/inspectionrecord.h"
#include "infrastructure/storage/databasemanager.h"

// RecordManager 负责检测记录的增删改查入口。
// 它在 DatabaseManager 之上提供业务记录级能力。
class RecordManager
{
public:
    explicit RecordManager(QString databasePath = QString());

    // 记录存取接口。
    bool initialize(QString *errorMessage = nullptr) const;
    bool saveRecord(const InspectionRecord &record, QString *errorMessage = nullptr) const;
    bool lookupRecordByInspectionId(
        const QString &inspectionId,
        InspectionRecord *record,
        QString *errorMessage = nullptr) const;
    QList<InspectionRecord> recentRecords(int limit = 10, QString *errorMessage = nullptr) const;
    QString databaseFilePath() const;

private:
    // 底层数据库与一次性初始化状态。
    DatabaseManager m_databaseManager;
    mutable QMutex m_initializeMutex;
    mutable bool m_isInitialized = false;
};
