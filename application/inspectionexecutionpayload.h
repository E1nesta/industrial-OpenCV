// 应用层载荷：InspectionExecutionPayload 定义一次巡检执行后的纯执行载荷。
// 该对象只承接 worker 链路中的请求、结果和结果图，不带分发策略。
#pragma once

#include <QMetaType>

#include <opencv2/core/mat.hpp>

#include "domain/entities/inspectionresult.h"
#include "domain/entities/inspectiontask.h"

struct InspectionExecutionPayload
{
    // 原始巡检任务请求。
    InspectionTask request;
    // 结构化巡检结果。
    InspectionResult result;
    // 带标注的结果图。
    cv::Mat annotatedImage;
};

Q_DECLARE_METATYPE(InspectionExecutionPayload)
