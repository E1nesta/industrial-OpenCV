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
