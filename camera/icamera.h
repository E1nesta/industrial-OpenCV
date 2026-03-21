#pragma once

#include <opencv2/core/mat.hpp>

class ICamera
{
public:
    virtual ~ICamera() = default;

    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpened() const = 0;
    virtual cv::Mat grabImage() = 0;
};

