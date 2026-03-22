#include "camera/captureworker.h"

#include <QtGlobal>

namespace
{
QString buildSourceDescription(const InputSourceConfig &config)
{
    if (!config.sourceName.trimmed().isEmpty()) {
        return config.sourceName.trimmed();
    }

    switch (config.type) {
    case InputSourceType::VideoFile:
        return config.sourcePath.trimmed().isEmpty() ? QStringLiteral("video")
                                                     : config.sourcePath;
    case InputSourceType::Camera:
        return QStringLiteral("camera-%1").arg(config.deviceIndex);
    case InputSourceType::FileImage:
    default:
        return config.sourcePath.trimmed().isEmpty() ? QStringLiteral("file") : config.sourcePath;
    }
}
} // namespace

CaptureWorker::CaptureWorker(QObject *parent)
    : QObject(parent)
{
    m_status.source = m_config;
    m_status.statusText = QStringLiteral("输入源未打开");
}

void CaptureWorker::openInputSource(const InputSourceConfig &config)
{
    m_config = config;
    m_status.lastFrameIndex = -1;
    publishStatus(CaptureState::Opening, false, QStringLiteral("正在打开输入源：%1").arg(sourceDescription()));

    if (m_previewTimer != nullptr && m_previewTimer->isActive()) {
        m_previewTimer->stop();
    }

    QString errorMessage;
    if (!m_source.open(config, &errorMessage)) {
        publishStatus(
            CaptureState::Error,
            false,
            errorMessage.isEmpty() ? QStringLiteral("输入源打开失败。") : errorMessage);
        return;
    }

    publishStatus(CaptureState::Idle, true, QStringLiteral("输入源已打开：%1").arg(sourceDescription()));
}

void CaptureWorker::closeInputSource()
{
    if (!m_source.isOpened() && (m_previewTimer == nullptr || !m_previewTimer->isActive())) {
        m_config = InputSourceConfig{};
        m_status.lastFrameIndex = -1;
        publishStatus(CaptureState::Idle, false, QStringLiteral("输入源已关闭。"));
        return;
    }

    publishStatus(CaptureState::Closing, m_source.isOpened(), QStringLiteral("正在关闭输入源。"));
    if (m_previewTimer != nullptr && m_previewTimer->isActive()) {
        m_previewTimer->stop();
    }
    m_source.close();
    m_config = InputSourceConfig{};
    m_status.lastFrameIndex = -1;
    publishStatus(CaptureState::Idle, false, QStringLiteral("输入源已关闭。"));
}

void CaptureWorker::startPreview()
{
    if (!m_source.isOpened()) {
        publishStatus(CaptureState::Error, false, QStringLiteral("输入源未打开，无法开始预览。"));
        return;
    }

    ensurePreviewTimer();
    if (!m_previewTimer->isActive()) {
        m_previewTimer->start(qMax(1, m_config.previewIntervalMs));
    }

    publishStatus(CaptureState::Previewing, true, QStringLiteral("正在预览：%1").arg(sourceDescription()));
}

void CaptureWorker::stopPreview()
{
    if (m_previewTimer != nullptr && m_previewTimer->isActive()) {
        m_previewTimer->stop();
    }

    publishStatus(
        CaptureState::Idle,
        m_source.isOpened(),
        m_source.isOpened() ? QStringLiteral("预览已停止。") : QStringLiteral("输入源未打开。"));
}

void CaptureWorker::onPreviewTimeout()
{
    CapturedFrame frame;
    QString errorMessage;
    if (!m_source.readFrame(&frame, &errorMessage)) {
        if (m_previewTimer != nullptr && m_previewTimer->isActive()) {
            m_previewTimer->stop();
        }
        m_status.lastFrameIndex = -1;
        if (m_config.type == InputSourceType::VideoFile) {
            publishStatus(
                CaptureState::Idle,
                m_source.isOpened(),
                errorMessage.isEmpty() ? QStringLiteral("视频回放已结束。") : errorMessage);
            return;
        }

        publishStatus(
            CaptureState::Error,
            m_source.isOpened(),
            errorMessage.isEmpty() ? QStringLiteral("读取预览帧失败。") : errorMessage);
        return;
    }

    m_status.lastFrameIndex = frame.meta.frameIndex;
    emit previewFrameReady(frame);
}

void CaptureWorker::ensurePreviewTimer()
{
    if (m_previewTimer != nullptr) {
        return;
    }

    m_previewTimer = new QTimer(this);
    m_previewTimer->setTimerType(Qt::PreciseTimer);
    connect(m_previewTimer, &QTimer::timeout, this, &CaptureWorker::onPreviewTimeout);
}

void CaptureWorker::publishStatus(CaptureState state, bool opened, const QString &statusText)
{
    m_status.state = state;
    m_status.source = m_config;
    m_status.opened = opened;
    m_status.statusText = statusText;
    emit captureStatusUpdated(m_status);
}

QString CaptureWorker::sourceDescription() const
{
    return buildSourceDescription(m_config);
}
