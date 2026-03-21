#pragma once

#include <QString>

#include "camera/icamera.h"

class VirtualCamera : public ICamera
{
public:
    bool open() override;
    void close() override;
    bool isOpened() const override;
    cv::Mat grabImage() override;

    bool loadImage(const QString &imagePath);

private:
    bool m_isOpened = false;
    QString m_imagePath;
};

