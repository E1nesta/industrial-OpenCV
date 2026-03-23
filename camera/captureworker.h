#pragma once

#include <QObject>
#include <QTimer>

#include "camera/videocapturesource.h"

// CaptureWorker 负责输入源生命周期和预览节拍控制。
// 它统一对外发布采集状态与最新预览帧，不承载检测逻辑。
class CaptureWorker : public QObject
{
    Q_OBJECT

public:
    explicit CaptureWorker(QObject *parent = nullptr);

signals:
    // 发给控制层/界面的采集状态与预览帧广播。
    void captureStatusUpdated(const CaptureStatusSnapshot &status);
    void previewFrameReady(const CapturedFrame &frame);

public slots:
    // 输入源控制入口：打开、关闭、开始预览、停止预览。
    void openInputSource(const InputSourceConfig &config);
    void closeInputSource();
    void startPreview();
    void stopPreview();

private slots:
    // 预览定时回调：按节拍拉取最新帧。
    void onPreviewTimeout();

private:
    // 内部辅助：定时器初始化、状态发布和来源描述。
    void ensurePreviewTimer();
    void publishStatus(CaptureState state, bool opened, const QString &statusText);
    QString sourceDescription() const;

    // 输入源对象与当前配置。
    VideoCaptureSource m_source;
    InputSourceConfig m_config;

    // 当前快照状态与预览定时器。
    CaptureStatusSnapshot m_status;
    QTimer *m_previewTimer = nullptr;

    // 视频回放态辅助标记。
    bool m_videoPlaybackFinished = false;
};
