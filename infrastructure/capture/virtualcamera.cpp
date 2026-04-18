#include "infrastructure/capture/virtualcamera.h"

#include <QFileInfo>

#include <opencv2/imgcodecs.hpp>

bool VirtualCamera::open()
{
    // 虚拟相机不占用设备句柄：有有效路径即可视为可打开。
    m_isOpened = !m_imagePath.isEmpty();
    return m_isOpened;
}

void VirtualCamera::close()
{
    m_isOpened = false;
}

bool VirtualCamera::isOpened() const
{
    return m_isOpened;
}

cv::Mat VirtualCamera::grabImage()
{
    // 只有在“已打开 + 路径有效”时才返回图像，失败返回空帧给上层判定。
    if (!m_isOpened || m_imagePath.isEmpty()) {
        return {};
    }

    return cv::imread(m_imagePath.toStdString(), cv::IMREAD_COLOR);
}

bool VirtualCamera::loadImage(const QString &imagePath)
{
    // 路径校验失败时不覆盖旧路径，避免误清空当前输入源。
    if (!QFileInfo::exists(imagePath)) {
        return false;
    }

    m_imagePath = imagePath;
    return true;
}
