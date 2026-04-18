// 领域实体：InputSource 定义输入源配置与采集状态快照。
// 该对象用于 UI、采集层和应用层之间共享输入源上下文。
#pragma once

#include <QMetaType>
#include <QString>

enum class InputSourceType
{
    // 单张图片输入。
    FileImage,
    // 视频文件输入。
    VideoFile,
    // 摄像头输入。
    Camera
};

enum class CaptureState
{
    // 空闲态。
    Idle,
    // 打开中。
    Opening,
    // 预览中。
    Previewing,
    // 关闭中。
    Closing,
    // 异常态。
    Error
};

struct InputSourceConfig
{
    // 输入源类型。
    InputSourceType type = InputSourceType::Camera;
    // 输入源路径。
    QString sourcePath;
    // 输入源名称。
    QString sourceName;
    // 摄像头设备索引。
    int deviceIndex = 0;
    // 预览拉帧间隔，单位毫秒。
    int previewIntervalMs = 33;
};

struct CaptureStatusSnapshot
{
    // 当前采集状态。
    CaptureState state = CaptureState::Idle;
    // 当前输入源配置快照。
    InputSourceConfig source;
    // 输入源是否已打开。
    bool opened = false;
    // 状态文本。
    QString statusText;
    // 最近一帧索引。
    qint64 lastFrameIndex = -1;
};

inline QString inputSourceTypeToString(InputSourceType type)
{
    // 把输入源类型转换为稳定字符串，用于配置持久化。
    switch (type) {
    case InputSourceType::VideoFile:
        return QStringLiteral("video");
    case InputSourceType::Camera:
        return QStringLiteral("camera");
    case InputSourceType::FileImage:
    default:
        return QStringLiteral("file");
    }
}

inline InputSourceType inputSourceTypeFromString(const QString &value)
{
    // 解析配置文本并回填输入源类型。
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("video")) {
        return InputSourceType::VideoFile;
    }
    if (normalized == QStringLiteral("camera")) {
        return InputSourceType::Camera;
    }
    return InputSourceType::FileImage;
}

Q_DECLARE_METATYPE(InputSourceType)
Q_DECLARE_METATYPE(CaptureState)
Q_DECLARE_METATYPE(InputSourceConfig)
Q_DECLARE_METATYPE(CaptureStatusSnapshot)
