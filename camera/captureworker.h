#pragma once

#include <QObject>
#include <QTimer>

#include "camera/videocapturesource.h"

class CaptureWorker : public QObject
{
    Q_OBJECT

public:
    explicit CaptureWorker(QObject *parent = nullptr);

public slots:
    void openInputSource(const InputSourceConfig &config);
    void closeInputSource();
    void startPreview();
    void stopPreview();

signals:
    void captureStatusUpdated(const CaptureStatusSnapshot &status);
    void previewFrameReady(const CapturedFrame &frame);

private slots:
    void onPreviewTimeout();

private:
    void ensurePreviewTimer();
    void publishStatus(CaptureState state, bool opened, const QString &statusText);
    QString sourceDescription() const;

    VideoCaptureSource m_source;
    InputSourceConfig m_config;
    CaptureStatusSnapshot m_status;
    QTimer *m_previewTimer = nullptr;
};
