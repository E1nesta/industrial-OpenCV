// 基础设施采集：cameramanager.cpp 负责输入源接入与帧获取。
// 本文件连接设备输入与应用层任务触发，隔离外设细节。
#include "infrastructure/capture/cameramanager.h"

bool CameraManager::loadLocalImage(const QString &imagePath)
{
    // 先加载路径，再进入“已打开”状态，保证 grabImage 可直接取帧。
    if (!m_virtualCamera.loadImage(imagePath)) {
        return false;
    }

    return m_virtualCamera.open();
}

cv::Mat CameraManager::grabImage()
{
    // 上层始终通过统一入口取帧，不感知底层是虚拟相机。
    return m_virtualCamera.grabImage();
}
