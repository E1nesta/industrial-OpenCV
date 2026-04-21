// 领域实体：InspectionResult 定义一次巡检的结构化业务结果。
// 该对象用于 UI 展示、结果存储和外部上报。
#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>

#include "domain/entities/capturedframe.h"
#include "domain/entities/defectitem.h"

struct InspectionResult
{
    // 巡检任务 ID。
    QString inspectionId;
    // 输入帧元信息。
    FrameMeta frameMeta;
    // 最终判定结果。
    bool isOk = true;
    // 缺陷数量。
    int defectCount = 0;
    // 处理耗时，单位毫秒。
    double elapsedMs = 0.0;
    // 失败原因。
    QString failureReason;
    // 结果摘要文本。
    QString summaryText;
    // 缺陷明细集合。
    QVector<DefectItem> defects;
};

Q_DECLARE_METATYPE(InspectionResult)
