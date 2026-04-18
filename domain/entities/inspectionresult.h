// 领域实体：InspectionResult 定义一次巡检的结构化业务结果。
// 该对象用于 UI 展示、结果存储和外部上报。
#pragma once

#include <QMetaType>
#include <QRect>
#include <QString>
#include <QVector>

#include "domain/entities/capturedframe.h"

struct InspectionResult
{
    // 巡检任务 ID。
    QString inspectionId;
    // 输入帧元信息。
    FrameMeta frameMeta;
    // 最终判定结果。
    bool isOk = true;
    // 是否由用户或系统取消。
    bool canceled = false;
    // 缺陷数量。
    int defectCount = 0;
    // 处理耗时，单位毫秒。
    double processTimeMs = 0.0;
    // 结果说明文本。
    QString message;
    // 缺陷框集合。
    QVector<QRect> defectRects;
};

Q_DECLARE_METATYPE(InspectionResult)
