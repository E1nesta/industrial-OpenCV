#pragma once

#include <QMetaType>
#include <QString>

enum class InputSourceType
{
    FileImage,
    VideoFile,
    Camera
};

enum class CaptureState
{
    Idle,
    Opening,
    Previewing,
    Closing,
    Error
};

struct InputSourceConfig
{
    InputSourceType type = InputSourceType::Camera;
    QString sourcePath;
    QString sourceName;
    int deviceIndex = 0;
    int previewIntervalMs = 33;
};

struct CaptureStatusSnapshot
{
    CaptureState state = CaptureState::Idle;
    InputSourceConfig source;
    bool opened = false;
    QString statusText;
    qint64 lastFrameIndex = -1;
};

inline QString inputSourceTypeToString(InputSourceType type)
{
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
