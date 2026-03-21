#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QThread>

#include "communication/tcpmanager.h"
#include "logger/logmanager.h"
#include "models/detectresult.h"
#include "models/deviceconfig.h"
#include "models/inspectionrecord.h"
#include "models/visionparam.h"
#include "storage/configmanager.h"
#include "storage/recordmanager.h"

class DetectionWorker;

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

    const VisionParam &visionParam() const noexcept;
    const DeviceConfig &deviceConfig() const noexcept;
    QString configFilePath() const;
    QString databaseFilePath() const;
    QString projectStage() const;
    QString statusMessage() const;
    QString tcpStatusText() const;
    bool isTcpConnected() const;
    bool isDetectionRunning() const noexcept;
    bool isDetectionCancelRequested() const noexcept;

signals:
    void statusChanged(const QString &message);
    void visionParamChanged();
    void deviceConfigChanged();
    void recordsChanged();
    void tcpStateChanged();
    void detectionRequested(const QString &imagePath, const VisionParam &param);
    void detectionStarted();
    void detectionFinished(const DetectResult &result, const QImage &resultImage);
    void detectionFailed(const QString &errorMessage);
    void detectionCanceled();
    void detectionRunningChanged(bool isRunning);

private:
    void updateStatus(const QString &message);
    void handleDetectionCompleted(const DetectResult &result, const QImage &resultImage);
    void handleDetectionFailed(const QString &errorMessage);
    void handleDetectionCanceled();

    VisionParam m_visionParam;
    DeviceConfig m_deviceConfig;
    ConfigManager m_configManager;
    RecordManager m_recordManager;
    LogManager m_logManager;
    TcpManager m_tcpManager;
    QThread m_detectionThread;
    DetectionWorker *m_detectionWorker = nullptr;
    bool m_isDetectionRunning = false;
    bool m_isDetectionCancelRequested = false;
    QString m_statusMessage;
};
