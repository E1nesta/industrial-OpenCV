#pragma once

#include <QObject>
#include <QString>

#include "domain/entities/inspectionoutput.h"
#include "domain/entities/persistenceresult.h"
#include "infrastructure/storage/recordmanager.h"

// InspectionPersistenceWorker 负责把检测结果落盘并写入记录库。
// 该 worker 在后台线程执行归档和存储，完成后回传统一持久化结果。
class InspectionPersistenceWorker : public QObject
{
    Q_OBJECT

public:
    explicit InspectionPersistenceWorker(QString databasePath = QString(), QObject *parent = nullptr);

signals:
    // 发给控制层的持久化完成回调信号。
    void persistenceCompleted(const PersistenceResult &result);

public slots:
    // 后台持久化入口：归档图片并保存数据库记录。
    void persist(const InspectionOutput &output);

private:
    // 内部辅助：路径解析、记录构建、图片归档。
    QString resolvedImageSaveDirectory(const InspectionOutput &output) const;
    InspectionRecord buildInspectionRecord(const InspectionOutput &output) const;
    bool archiveInspectionImages(
        const InspectionOutput &output,
        InspectionRecord &record,
        QString *errorMessage) const;

    // 记录读写入口。
    RecordManager m_recordManager;
};
