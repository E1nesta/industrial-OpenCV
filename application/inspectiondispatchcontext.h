// 应用层载荷：InspectionDispatchContext 定义一次巡检执行后的分发上下文。
// 该对象在纯执行结果基础上补充分发策略，供应用层出口编排使用。
#pragma once

#include <QMetaType>

#include "application/inspectionexecutionpayload.h"
#include "domain/entities/deviceconfig.h"

struct InspectionDispatchContext
{
    // 纯执行结果：请求、检测结论和结果图。
    InspectionExecutionPayload execution;
    // 是否需要分发 TCP 结果。
    bool shouldSendTcpResult = false;
    // 分发 TCP 结果时使用的设备配置。
    DeviceConfig tcpDeviceConfig;
};

Q_DECLARE_METATYPE(InspectionDispatchContext)
