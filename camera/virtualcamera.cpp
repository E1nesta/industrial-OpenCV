#include "camera/virtualcamera.h"

#include <QFileInfo>

#include <opencv2/imgcodecs.hpp>

bool VirtualCamera::open()
{
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
    if (!m_isOpened || m_imagePath.isEmpty()) {
        return {};
    }

    return cv::imread(m_imagePath.toStdString(), cv::IMREAD_COLOR);
}

bool VirtualCamera::loadImage(const QString &imagePath)
{
    if (!QFileInfo::exists(imagePath)) {
        return false;
    }

    m_imagePath = imagePath;
    return true;
}

