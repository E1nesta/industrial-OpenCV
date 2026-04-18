#include "infrastructure/capture/captureworker.h"

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
    // 初始状态默认未打开输入源，等待控制层显式下发打开请求。
    m_status.source = m_config;
    m_status.statusText = QStringLiteral("输入源未打开");
}

void CaptureWorker::openInputSource(const InputSourceConfig &config)
{
    // 打开新输入源前，先重置上一轮预览状态和帧索引。
    m_config = config;
    m_status.lastFrameIndex = -1;
    m_videoPlaybackFinished = false;
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
    // 关闭入口统一清理预览状态和输入源上下文。
    if (!m_source.isOpened() && (m_previewTimer == nullptr || !m_previewTimer->isActive())) {
        m_config = InputSourceConfig{};
        m_status.lastFrameIndex = -1;
        m_videoPlaybackFinished = false;
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
    m_videoPlaybackFinished = false;
    publishStatus(CaptureState::Idle, false, QStringLiteral("输入源已关闭。"));
}

void CaptureWorker::startPreview()
{
    // 视频文件模式支持“回放结束后重新开始”，先尝试回到首帧。
    if (!m_source.isOpened()) {
        publishStatus(CaptureState::Error, false, QStringLiteral("输入源未打开，无法开始预览。"));
        return;
    }

    if (m_videoPlaybackFinished && m_source.supportsPreviewRestart()) {
        QString rewindError;
        if (!m_source.rewind(&rewindError)) {
            publishStatus(
                CaptureState::Error,
                m_source.isOpened(),
                rewindError.isEmpty() ? QStringLiteral("视频回放重置失败。") : rewindError);
            return;
        }
        m_status.lastFrameIndex = -1;
        m_videoPlaybackFinished = false;
    }

    ensurePreviewTimer();
    if (!m_previewTimer->isActive()) {
        m_previewTimer->start(qMax(1, m_config.previewIntervalMs));
    }

    m_status.lastFrameIndex = -1;
    publishStatus(CaptureState::Previewing, true, QStringLiteral("正在预览：%1").arg(sourceDescription()));
}

void CaptureWorker::stopPreview()
{
    // 停止预览只影响预览节拍，不主动关闭输入源本身。
    if (m_previewTimer != nullptr && m_previewTimer->isActive()) {
        m_previewTimer->stop();
    }

    m_status.lastFrameIndex = -1;
    publishStatus(
        CaptureState::Idle,
        m_source.isOpened(),
        m_source.isOpened() ? QStringLiteral("预览已停止。") : QStringLiteral("输入源未打开。"));
}

void CaptureWorker::onPreviewTimeout()
{
    // 定时拉帧：只在这里和底层输入源交互，统一处理流结束与读取失败。
    CapturedFrame frame;
    QString errorMessage;
    const FrameReadStatus readStatus = m_source.readFrame(&frame, &errorMessage);
    if (readStatus != FrameReadStatus::Ok) {
        if (m_previewTimer != nullptr && m_previewTimer->isActive()) {
            m_previewTimer->stop();
        }
        if (readStatus == FrameReadStatus::EndOfStream && m_config.type == InputSourceType::VideoFile) {
            m_videoPlaybackFinished = true;
            publishStatus(
                CaptureState::Idle,
                m_source.isOpened(),
                errorMessage.isEmpty() ? QStringLiteral("视频回放已结束，可重新开始预览。")
                                       : QStringLiteral("%1 可重新开始预览。").arg(errorMessage));
            return;
        }

        m_videoPlaybackFinished = false;
        m_status.lastFrameIndex = -1;
        publishStatus(
            CaptureState::Error,
            m_source.isOpened(),
            errorMessage.isEmpty() ? QStringLiteral("读取预览帧失败。") : errorMessage);
        return;
    }

    m_videoPlaybackFinished = false;
    m_status.lastFrameIndex = frame.meta.frameIndex;
    // 预览链只分发最新帧，不在采集层做额外处理。
    emit previewFrameReady(frame);
}

void CaptureWorker::ensurePreviewTimer()
{
    // 预览定时器按需懒创建，避免未预览时占用额外对象。
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
    // 所有状态更新统一经由此处发给控制层，避免状态来源分散。
    emit captureStatusUpdated(m_status);
}

QString CaptureWorker::sourceDescription() const
{
    // 状态文案统一走来源描述，避免散落拼接逻辑。
    return buildSourceDescription(m_config);
}
