// 领域实体：InspectionRecord 定义落库记录模型。
// 该对象用于最近记录列表展示与历史回看。
#pragma once

#include <QMetaType>
#include <QString>

#include "domain/entities/inputsource.h"

struct InspectionRecord
{
    // 巡检任务 ID。
    QString inspectionId;
    // 输入帧采集 ID。
    QString captureId;
    // 记录时间戳文本。
    QString timestamp;
    // 批次号。
    QString batchNo;
    // 配方名称。
    QString recipeName;
    // 输入源类型。
    InputSourceType sourceType = InputSourceType::FileImage;
    // 输入源路径。
    QString sourcePath;
    // 输入源名称。
    QString sourceName;
    // 帧序号。
    qint64 frameIndex = -1;
    // 最终判定结果。
    bool isOk = true;
    // 缺陷数量。
    int defectCount = 0;
    // 处理耗时，单位毫秒。
    double processTimeMs = 0.0;
    // 结果摘要。
    QString summaryText;
    // 原图归档路径。
    QString imagePath;
    // 结果图归档路径。
    QString resultImagePath;
};

Q_DECLARE_METATYPE(InspectionRecord)
