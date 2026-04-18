// 应用层编排：ResultDispatcher 负责把巡检输出分发到展示、持久化和通信出口。
// 它负责结果收敛，不负责具体存储实现和网络协议细节。
#pragma once

#include <functional>

#include <QImage>
#include <QString>

#include "domain/entities/deviceconfig.h"
#include "domain/entities/inspectionoutput.h"

class LogManager;

struct ResultDispatchOutcome
{
    // 结构化业务结果，供 UI 与统计逻辑复用。
    InspectionResult result;
    // 主线程可直接显示的结果图像。
    QImage resultImage;
    // 状态栏与提示区域展示文本。
    QString statusMessage;
};

class ResultDispatcher
{
public:
    // 分发入口：收敛结果并触发持久化/TCP 出口回调。
    ResultDispatchOutcome dispatch(
        const InspectionOutput &output,
        LogManager *logManager,
        const std::function<void(const InspectionOutput &)> &persistenceSink,
        const std::function<void(const QString &, bool, const DeviceConfig &)> &tcpSink) const;
};
