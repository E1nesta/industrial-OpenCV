#pragma once

#include <opencv2/core/mat.hpp>

// ICamera 定义统一采集接口：
// 上层只依赖打开/关闭/取帧，不关心具体来源是摄像头、视频还是图片。
class ICamera
{
public:
    virtual ~ICamera() = default;

    // 生命周期控制。
    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpened() const = 0;

    // 拉取一帧彩色图像；失败时返回空 Mat。
    virtual cv::Mat grabImage() = 0;
};
