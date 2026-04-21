// 领域实体：InspectionTask 定义一次巡检任务的纯输入上下文。
// 该对象只描述“检测什么”，不混入通信和分发策略。
#pragma once

#include <QMetaType>
#include <QString>

#include "domain/entities/capturedframe.h"
#include "domain/entities/recipe.h"

struct InspectionTask
{
    // 巡检任务唯一 ID。
    QString inspectionId;
    // 待巡检输入帧。
    CapturedFrame frame;
    // 本次巡检配方参数。
    Recipe recipe;
};

Q_DECLARE_METATYPE(InspectionTask)
