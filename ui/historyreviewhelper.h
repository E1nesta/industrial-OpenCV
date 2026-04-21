// 表现层辅助：historyreviewhelper 负责收敛历史记录回看所需内容。
// 本文件只处理回看资源解析，不直接操作控件。
#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

#include "domain/entities/inspectionrecord.h"

struct HistoryReviewContent
{
    // 可用于重新发起检测的原图。
    QImage sourceImage;
    // 可用于结果回看的结果图。
    QImage resultImage;
    // 只有原图可用时才允许回填为新的检测输入。
    QString inspectionInputPath;
    QString inspectionInputName;
    // 界面上当前图像区域显示的提示文本。
    QString currentImageLabel;
    // 状态栏建议展示文本。
    QString statusMessage;
    // 解析阶段产生的警告文本，供日志输出。
    QStringList warnings;
    // 是否成功拿到可显示原图。
    bool sourceImageReady = false;
    // 是否成功拿到可显示结果图。
    bool resultImageReady = false;

    bool hasDisplayableImage() const noexcept
    {
        return sourceImageReady || resultImageReady;
    }

    bool canReuseAsInspectionInput() const noexcept
    {
        return sourceImageReady && !inspectionInputPath.isEmpty();
    }
};

// 历史回看入口：解析原图/结果图并给出回看与复检所需状态。
HistoryReviewContent loadHistoryReviewContent(const InspectionRecord &record);
