#include "camera/videocapturesource.h"

#include <QDateTime>
#include <QFileInfo>
#include <QUuid>

namespace
{
QString describeSource(const InputSourceConfig &config)
{
    if (!config.sourceName.trimmed().isEmpty()) {
        return config.sourceName.trimmed();
    }

    switch (config.type) {
    case InputSourceType::VideoFile:
        return QFileInfo(config.sourcePath).fileName();
    case InputSourceType::Camera:
        return QStringLiteral("camera-%1").arg(config.deviceIndex);
    case InputSourceType::FileImage:
    default:
        return QFileInfo(config.sourcePath).fileName();
    }
}
} // namespace

bool VideoCaptureSource::open(const InputSourceConfig &config, QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    close();

    if (config.type == InputSourceType::FileImage) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("静态图片不通过 VideoCapture 输入源打开。");
        }
        return false;
    }

    bool opened = false;
    if (config.type == InputSourceType::Camera) {
        opened = m_capture.open(config.deviceIndex, cv::CAP_ANY);
    } else {
        if (config.sourcePath.trimmed().isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("视频文件路径为空。");
            }
            return false;
        }
        opened = m_capture.open(config.sourcePath.toStdString(), cv::CAP_ANY);
    }

    if (!opened) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法打开输入源：%1").arg(describeSource(config));
        }
        m_capture.release();
        return false;
    }

    m_config = config;
    m_nextFrameIndex = 0;
    return true;
}

void VideoCaptureSource::close()
{
    if (m_capture.isOpened()) {
        m_capture.release();
    }

    m_config = InputSourceConfig{};
    m_nextFrameIndex = 0;
}

bool VideoCaptureSource::isOpened() const
{
    return m_capture.isOpened();
}

bool VideoCaptureSource::readFrame(CapturedFrame *frame, QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (frame == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("输出帧对象为空。");
        }
        return false;
    }
    if (!m_capture.isOpened()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("输入源尚未打开。");
        }
        return false;
    }

    cv::Mat rawFrame;
    if (!m_capture.read(rawFrame) || rawFrame.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = m_config.type == InputSourceType::VideoFile
                                ? QStringLiteral("视频回放已结束或读取失败。")
                                : QStringLiteral("摄像头读取帧失败。");
        }
        return false;
    }

    CapturedFrame capturedFrame;
    capturedFrame.meta.captureId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    capturedFrame.meta.sourceType = m_config.type;
    capturedFrame.meta.sourcePath =
        m_config.type == InputSourceType::Camera ? QString() : m_config.sourcePath;
    capturedFrame.meta.sourceName = effectiveSourceName();
    capturedFrame.meta.frameIndex = m_nextFrameIndex++;
    capturedFrame.meta.capturedAt = QDateTime::currentDateTime();
    capturedFrame.image = rawFrame.clone();

    if (!capturedFrame.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("输入源帧拷贝失败。");
        }
        return false;
    }

    *frame = std::move(capturedFrame);
    return true;
}

const InputSourceConfig &VideoCaptureSource::config() const noexcept
{
    return m_config;
}

QString VideoCaptureSource::effectiveSourceName() const
{
    return describeSource(m_config);
}
