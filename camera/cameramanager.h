#pragma once

#include <QString>

#include <opencv2/core/mat.hpp>

#include "camera/virtualcamera.h"

class CameraManager
{
public:
    bool loadLocalImage(const QString &imagePath);
    cv::Mat grabImage();

private:
    VirtualCamera m_virtualCamera;
};

