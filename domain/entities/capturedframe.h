// 领域实体：CapturedFrame 定义采集帧数据与元信息。
// 该对象是采集链与巡检链共享的统一输入载体。
#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

#include <opencv2/core/mat.hpp>

#include "domain/entities/inputsource.h"

struct FrameMeta
{
    // 本次采集唯一 ID。
    QString captureId;
    // 输入源类型。
    InputSourceType sourceType = InputSourceType::FileImage;
    // 输入源路径。
    QString sourcePath;
    // 输入源显示名称。
    QString sourceName;
    // 帧序号，未知时为 -1。
    qint64 frameIndex = -1;
    // 采集时间戳。
    QDateTime capturedAt;
};

struct CapturedFrame
{
    // 帧元信息。
    FrameMeta meta;
    // 原始图像数据。
    cv::Mat image;

    // 有效性判定：图像缓冲区非空视为有效。
    bool isValid() const
    {
        return !image.empty();
    }
};

Q_DECLARE_METATYPE(FrameMeta)
Q_DECLARE_METATYPE(CapturedFrame)
