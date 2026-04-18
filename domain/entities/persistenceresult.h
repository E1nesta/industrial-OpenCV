// 领域实体：PersistenceResult 定义持久化链路的执行结果。
// 该对象用于反馈图片归档与记录写入是否成功。
#pragma once

#include <QMetaType>
#include <QString>

#include "domain/entities/inspectionrecord.h"

struct PersistenceResult
{
    // 巡检任务 ID。
    QString inspectionId;
    // 输入帧采集 ID。
    QString captureId;
    // 图片归档是否成功。
    bool archiveSucceeded = false;
    // 记录写入是否成功。
    bool recordSaved = false;
    // 原图归档路径。
    QString archivedSourcePath;
    // 结果图归档路径。
    QString resultImagePath;
    // 写入数据库的记录对象。
    InspectionRecord record;
    // 归档流程说明文本。
    QString archiveMessage;
    // 记录写入错误文本。
    QString recordError;
};

Q_DECLARE_METATYPE(PersistenceResult)
