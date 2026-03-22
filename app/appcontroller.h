#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QThread>

#include "models/capturedframe.h"
#include "logger/logmanager.h"
#include "models/detectionoutput.h"
#include "models/detectionrequest.h"
#include "models/detectresult.h"
#include "models/deviceconfig.h"
#include "models/inputsource.h"
#include "models/inspectionrecord.h"
#include "models/persistenceresult.h"
#include "models/visionparam.h"
#include "storage/configmanager.h"
#include "storage/inspectionpersistenceworker.h"
#include "storage/recordmanager.h"

class DetectionWorker;
class CaptureWorker;
class TcpWorker;
class QTimer;

class AppController : public QObject
{
    Q_OBJECT

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    void initialize();
    void reloadConfig();
    void saveCurrentParam();
    void resetToDefaults();
    void setVisionParam(const VisionParam &param);
    void setDeviceConfig(const DeviceConfig &config);
    void setInputSourceConfig(const InputSourceConfig &config);
    bool startDetection(const QString &imagePath);
    bool openInputSource();
    void closeInputSource();
    bool startPreview();
    void stopPreview();
    bool detectCurrentFrame();
    bool cancelDetection();
    bool connectTcpDevice();
    void disconnectTcpDevice();
    QList<InspectionRecord> recentRecords(int limit = 8) const;
    bool lookupRecordByInspectionId(
        const QString &inspectionId,
        InspectionRecord *record,
        QString *errorMessage = nullptr) const;

    LogManager &logManager() noexcept;
    const LogManager &logManager() const noexcept;
    QString activeInspectionId() const;
    QString lastCompletedInspectionId() const;
    const VisionParam &visionParam() const noexcept;
    const DeviceConfig &deviceConfig() const noexcept;
    const InputSourceConfig &inputSourceConfig() const noexcept;
    const CaptureStatusSnapshot &captureStatus() const noexcept;
    QString configFilePath() const;
    QString databaseFilePath() const;
    QString projectStage() const;
    QString statusMessage() const;
    QString tcpStatusText() const;
    bool isTcpConnected() const;
    bool isTcpOperationPending() const noexcept;
    bool hasLatestFrame() const noexcept;
    bool isDetectionRunning() const noexcept;
    bool isDetectionCancelRequested() const noexcept;

signals:
    void statusChanged(const QString &message);
    void visionParamChanged();
    void deviceConfigChanged();
    void inputSourceConfigChanged();
    void captureStatusChanged(const CaptureStatusSnapshot &status);
    void previewFrameUpdated(const QImage &previewImage);
    void recordsChanged();
    void tcpStateChanged();
    void captureOpenRequested(const InputSourceConfig &config);
    void captureCloseRequested();
    void captureStartPreviewRequested();
    void captureStopPreviewRequested();
    void detectionRequested(const DetectionRequest &request);
    void detectionStarted();
    void detectionFinished(const DetectResult &result, const QImage &resultImage);
    void detectionFailed(const QString &errorMessage);
    void detectionCanceled();
    void detectionRunningChanged(bool isRunning);
    void persistenceRequested(const DetectionOutput &output);
    void tcpConfigRequested(const DeviceConfig &config, bool disconnectFirst);
    void tcpConnectRequested(const DeviceConfig &config);
    void tcpDisconnectRequested();
    void tcpSendRequested(const QString &inspectionId, bool isOk);

private:
    enum class TcpOperationState
    {
        Idle,
        Connecting,
        Disconnecting
    };

    void updateStatus(const QString &message);
    bool buildFrameDetectionRequest(
        const CapturedFrame &frame,
        const QString &inspectionId,
        DetectionRequest *request,
        QString *errorMessage) const;
    bool buildFileDetectionRequest(
        const QString &imagePath,
        const QString &inspectionId,
        DetectionRequest *request,
        QString *errorMessage) const;
    void handleCaptureStatusUpdated(const CaptureStatusSnapshot &status);
    void handlePreviewFrameReady(const CapturedFrame &frame);
    void renderLatestPreviewFrame();
    void handleDetectionCompleted(const DetectionOutput &output);
    void handleDetectionFailed(const QString &inspectionId, const QString &errorMessage);
    void handleDetectionCanceled(const QString &inspectionId);
    void handlePersistenceCompleted(const PersistenceResult &result);
    void handleTcpSendCompleted(
        const QString &inspectionId,
        bool success,
        const QString &reply,
        const QString &error,
        const QString &peerDescription,
        const QString &statusText,
        bool connected);
    void handleTcpConnectCompleted(
        bool success,
        const QString &error,
        const QString &peerDescription,
        const QString &statusText,
        bool connected);
    void handleTcpConfigApplied(const QString &statusText, bool connected);
    void handleTcpDisconnectCompleted(
        const QString &peerDescription,
        const QString &statusText,
        bool connected);

    VisionParam m_visionParam;
    DeviceConfig m_deviceConfig;
    ConfigManager m_configManager;
    RecordManager m_recordManager;
    mutable LogManager m_logManager;
    QThread m_captureThread;
    CaptureWorker *m_captureWorker = nullptr;
    QThread m_detectionThread;
    DetectionWorker *m_detectionWorker = nullptr;
    QThread m_persistenceThread;
    InspectionPersistenceWorker *m_persistenceWorker = nullptr;
    QThread m_tcpThread;
    TcpWorker *m_tcpWorker = nullptr;
    InputSourceConfig m_inputSourceConfig;
    CaptureStatusSnapshot m_captureStatus;
    CapturedFrame m_latestFrame;
    CapturedFrame m_latestPreviewFrame;
    bool m_previewFrameObserved = false;
    bool m_previewFrameDelivered = false;
    qint64 m_lastRenderedPreviewFrameIndex = -1;
    quint64 m_previewReceivedCount = 0;
    quint64 m_previewRenderedCount = 0;
    quint64 m_previewOverwrittenCount = 0;
    QTimer *m_previewRenderTimer = nullptr;
    bool m_isTcpConnected = false;
    QString m_tcpStatusText = QStringLiteral("未连接");
    TcpOperationState m_tcpOperationState = TcpOperationState::Idle;
    bool m_isDetectionRunning = false;
    bool m_isDetectionCancelRequested = false;
    QString m_activeInspectionId;
    QString m_lastCompletedInspectionId;
    QString m_statusMessage;
};
