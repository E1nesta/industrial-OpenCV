#pragma once

#include <QString>

#include "infrastructure/capture/icamera.h"

// VirtualCamera 把本地图片包装成 ICamera：
// 让“单张图片输入”复用与摄像头一致的取帧接口。
class VirtualCamera : public ICamera
{
public:
    // ICamera 接口实现。
    bool open() override;
    void close() override;
    bool isOpened() const override;
    cv::Mat grabImage() override;

    // 仅记录图片路径并做存在性检查；真正读图在 grabImage 时执行。
    bool loadImage(const QString &imagePath);

private:
    // 输入源状态。
    bool m_isOpened = false;
    QString m_imagePath;
};
