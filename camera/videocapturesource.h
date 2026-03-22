#pragma once

#include <QString>

#include <opencv2/videoio.hpp>

#include "models/capturedframe.h"
#include "models/inputsource.h"

class VideoCaptureSource
{
public:
    bool open(const InputSourceConfig &config, QString *errorMessage = nullptr);
    void close();
    bool isOpened() const;
    bool readFrame(CapturedFrame *frame, QString *errorMessage = nullptr);
    const InputSourceConfig &config() const noexcept;

private:
    QString effectiveSourceName() const;

    cv::VideoCapture m_capture;
    InputSourceConfig m_config;
    qint64 m_nextFrameIndex = 0;
};
