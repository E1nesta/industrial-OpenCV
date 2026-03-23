#pragma once

#include <QString>

#include <opencv2/videoio.hpp>

#include "models/capturedframe.h"
#include "models/inputsource.h"

enum class FrameReadStatus
{
    Ok,
    EndOfStream,
    Failed
};

// VideoCaptureSource 统一封装视频文件和摄像头的 OpenCV 取帧入口。
// 该类负责打开/关闭、读帧和视频回放重置，不负责预览定时与状态广播。
class VideoCaptureSource
{
public:
    // 输入源生命周期。
    bool open(const InputSourceConfig &config, QString *errorMessage = nullptr);
    void close();
    bool isOpened() const;

    // 帧读取与回放控制。
    FrameReadStatus readFrame(CapturedFrame *frame, QString *errorMessage = nullptr);
    bool rewind(QString *errorMessage = nullptr);
    bool supportsPreviewRestart() const;

    // 当前输入源配置快照。
    const InputSourceConfig &config() const noexcept;

private:
    QString effectiveSourceName() const;

    // OpenCV 采集对象与当前配置上下文。
    cv::VideoCapture m_capture;
    InputSourceConfig m_config;

    // 输出帧递增编号。
    qint64 m_nextFrameIndex = 0;
};
