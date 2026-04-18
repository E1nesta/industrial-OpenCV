// 基础设施采集：videocapturesource.cpp 负责输入源接入与帧获取。
// 本文件连接设备输入与应用层任务触发，隔离外设细节。
#include "infrastructure/capture/videocapturesource.h"

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
    // 输入源打开入口：按类型打开摄像头或视频文件，并重置帧索引状态。
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    close();

    // 静态图片走单次检测链，不通过 VideoCapture 打开。
    if (config.type == InputSourceType::FileImage) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("静态图片不通过 VideoCapture 输入源打开。");
        }
        return false;
    }

    bool opened = false;
    // 摄像头和视频文件共用同一采集接口，向上层暴露统一帧模型。
    if (config.type == InputSourceType::Camera) {
        opened = m_capture.open(config.deviceIndex, cv::CAP_ANY);
    } else {
        if (config.sourcePath.trimmed().isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("视频文件路径为空。");
            }
            return false;
        }
        const QFileInfo sourceFileInfo(config.sourcePath);
        if (!sourceFileInfo.exists() || !sourceFileInfo.isFile()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("视频文件不存在：%1").arg(config.sourcePath);
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
    // 关闭后重置配置与帧序号，保证下一次打开从干净状态开始。
    if (m_capture.isOpened()) {
        m_capture.release();
    }

    m_config = InputSourceConfig{};
    m_nextFrameIndex = 0;
}

bool VideoCaptureSource::isOpened() const
{
    // 统一透传底层 VideoCapture 打开状态。
    return m_capture.isOpened();
}

FrameReadStatus VideoCaptureSource::readFrame(CapturedFrame *frame, QString *errorMessage)
{
    // 读帧入口：统一封装底层读帧结果，并区分“流结束”和“读取失败”。
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (frame == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("输出帧对象为空。");
        }
        return FrameReadStatus::Failed;
    }
    if (!m_capture.isOpened()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("输入源尚未打开。");
        }
        return FrameReadStatus::Failed;
    }

    cv::Mat rawFrame;
    // 读取失败时区分“视频自然结束”和“异常失败”，便于上层给出正确状态。
    if (!m_capture.read(rawFrame) || rawFrame.empty()) {
        if (m_config.type == InputSourceType::VideoFile) {
            const double frameCount = m_capture.get(cv::CAP_PROP_FRAME_COUNT);
            const double currentFrame = m_capture.get(cv::CAP_PROP_POS_FRAMES);
            const double streamRatio = m_capture.get(cv::CAP_PROP_POS_AVI_RATIO);
            const bool reachedEndByFrameCount = frameCount > 0.0 && currentFrame >= frameCount;
            const bool reachedEndByRatio =
                currentFrame > 0.0 && streamRatio >= 0.999 && streamRatio <= 1.001;
            const bool reachedEnd = reachedEndByFrameCount || reachedEndByRatio;
            if (reachedEnd) {
                if (errorMessage != nullptr) {
                    *errorMessage = QStringLiteral("视频回放已结束。");
                }
                return FrameReadStatus::EndOfStream;
            }
        }

        if (errorMessage != nullptr) {
            *errorMessage = m_config.type == InputSourceType::VideoFile
                                ? QStringLiteral("视频帧读取失败。")
                                : QStringLiteral("摄像头读取帧失败。");
        }
        return FrameReadStatus::Failed;
    }

    // 将底层 cv::Mat 封装为统一 CapturedFrame，补齐来源与时间元数据。
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
        return FrameReadStatus::Failed;
    }

    *frame = std::move(capturedFrame);
    return FrameReadStatus::Ok;
}

bool VideoCaptureSource::rewind(QString *errorMessage)
{
    // 视频回放重启入口：仅视频文件模式支持回到首帧。
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    if (!isOpened()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("输入源尚未打开，无法重新开始。");
        }
        return false;
    }

    if (m_config.type != InputSourceType::VideoFile) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("当前输入源不支持重新开始预览。");
        }
        return false;
    }

    // 预览重启只支持视频文件：回到首帧并重置帧序号。
    const bool seekOk = m_capture.set(cv::CAP_PROP_POS_FRAMES, 0.0);
    if (!seekOk) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("视频输入源不支持回到首帧。");
        }
        return false;
    }

    m_nextFrameIndex = 0;
    return true;
}

bool VideoCaptureSource::supportsPreviewRestart() const
{
    // 仅视频文件模式支持“回到首帧后继续预览”。
    return isOpened() && m_config.type == InputSourceType::VideoFile;
}

const InputSourceConfig &VideoCaptureSource::config() const noexcept
{
    return m_config;
}

QString VideoCaptureSource::effectiveSourceName() const
{
    // 来源名称优先使用配置值，缺省时按类型回退。
    return describeSource(m_config);
}
