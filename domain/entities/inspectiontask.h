// 领域实体：InspectionTask 定义一次巡检任务的完整输入上下文。
// 该对象在应用层与巡检执行层之间传递。
#pragma once

#include <QMetaType>
#include <QString>

#include "domain/entities/capturedframe.h"
#include "domain/entities/deviceconfig.h"
#include "domain/entities/recipe.h"

struct InspectionTask
{
    // 巡检任务唯一 ID。
    QString inspectionId;
    // 待巡检输入帧。
    CapturedFrame frame;
    // 本次巡检配方参数。
    Recipe recipe;
    // 是否需要发送 TCP 结果。
    bool shouldSendTcpResult = false;
    // 发送 TCP 时使用的设备配置。
    DeviceConfig tcpDeviceConfig;
};

Q_DECLARE_METATYPE(InspectionTask)
