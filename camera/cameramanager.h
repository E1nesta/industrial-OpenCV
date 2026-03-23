#pragma once

#include <QString>

#include <opencv2/core/mat.hpp>

#include "camera/virtualcamera.h"

// CameraManager 提供“图片即相机”的轻量入口：
// 负责把本地图片加载为虚拟采集源，并向上层暴露统一取帧接口。
class CameraManager
{
public:
    // 本地图片加载入口：成功后可通过 grabImage 取到图像帧。
    bool loadLocalImage(const QString &imagePath);
    cv::Mat grabImage();

private:
    // 当前仅保留虚拟相机实现，后续可替换为真实设备实现。
    VirtualCamera m_virtualCamera;
};
