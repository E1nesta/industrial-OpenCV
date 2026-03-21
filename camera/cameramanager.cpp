#include "camera/cameramanager.h"

bool CameraManager::loadLocalImage(const QString &imagePath)
{
    if (!m_virtualCamera.loadImage(imagePath)) {
        return false;
    }

    return m_virtualCamera.open();
}

cv::Mat CameraManager::grabImage()
{
    return m_virtualCamera.grabImage();
}

