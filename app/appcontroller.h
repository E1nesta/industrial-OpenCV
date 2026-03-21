#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QThread>

#include "logger/logmanager.h"
#include "models/detectresult.h"
#include "models/deviceconfig.h"
#include "models/inspectionrecord.h"
#include "models/visionparam.h"
#include "storage/configmanager.h"
#include "storage/inspectionpersistenceworker.h"
#include "storage/recordmanager.h"

class DetectionWorker;
class TcpWorker;

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
    bool startDetection(const QString &imagePath);
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
    QString configFilePath() const;
    QString databaseFilePath() const;
    QString projectStage() const;
    QString statusMessage() const;
    QString tcpStatusText() const;
    bool isTcpConnected() const;
    bool isTcpOperationPending() const noexcept;
    bool isDetectionRunning() const noexcept;
    bool isDetectionCancelRequested() const noexcept;

signals:
    void statusChanged(const QString &message);
    void visionParamChanged();
    void deviceConfigChanged();
    void recordsChanged();
    void tcpStateChanged();
    void detectionRequested(const QString &inspectionId, const QString &imagePath, const VisionParam &param);
    void detectionStarted();
    void detectionFinished(const DetectResult &result, const QImage &resultImage);
    void detectionFailed(const QString &errorMessage);
    void detectionCanceled();
    void detectionRunningChanged(bool isRunning);
    void persistenceRequested(const DetectResult &result, const QImage &resultImage, const VisionParam &param);
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
    void handleDetectionCompleted(const DetectResult &result, const QImage &resultImage);
    void handleDetectionFailed(const QString &errorMessage);
    void handleDetectionCanceled();
    void handlePersistenceCompleted(
        const InspectionRecord &record,
        bool archiveSucceeded,
        const QString &archiveMessage,
        bool recordSaved,
        const QString &recordError);
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
    QThread m_detectionThread;
    DetectionWorker *m_detectionWorker = nullptr;
    QThread m_persistenceThread;
    InspectionPersistenceWorker *m_persistenceWorker = nullptr;
    QThread m_tcpThread;
    TcpWorker *m_tcpWorker = nullptr;
    bool m_isTcpConnected = false;
    QString m_tcpStatusText = QStringLiteral("未连接");
    TcpOperationState m_tcpOperationState = TcpOperationState::Idle;
    bool m_isDetectionRunning = false;
    bool m_isDetectionCancelRequested = false;
    QString m_activeInspectionId;
    QString m_lastCompletedInspectionId;
    QString m_statusMessage;
};
