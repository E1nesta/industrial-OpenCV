// 领域实体：InspectionOutput 定义巡检执行层的完整输出。
// 该对象同时承载任务输入、业务结果和结果图像。
#pragma once

#include <QMetaType>

#include <opencv2/core/mat.hpp>

#include "domain/entities/inspectionresult.h"
#include "domain/entities/inspectiontask.h"

struct InspectionOutput
{
    // 原始巡检任务请求。
    InspectionTask request;
    // 结构化巡检结果。
    InspectionResult result;
    // 带标注的结果图。
    cv::Mat annotatedImage;
};

Q_DECLARE_METATYPE(InspectionOutput)
