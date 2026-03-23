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

// AppController 是应用编排中心：
// 负责串联采集、检测、持久化和 TCP 通信，并对 UI 暴露统一状态。
class AppController : public QObject
{
    Q_OBJECT

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    // 生命周期与配置管理。
    void initialize();
    void reloadConfig();
    void saveCurrentParam();
    void resetToDefaults();
    void setVisionParam(const VisionParam &param);
    void setDeviceConfig(const DeviceConfig &config);
    void setInputSourceConfig(const InputSourceConfig &config);

    // 检测入口：图片单次检测、当前帧检测、连续检测。
    bool startDetection(const QString &imagePath);
    bool openInputSource();
    void closeInputSource();
    bool startPreview();
    void stopPreview();
    // 仅对当前最新帧触发一次检测，适用于视频/摄像头预览场景。
    bool detectCurrentFrame();
    // 连续检测采用“最新帧 + 单任务门控”策略，不排队补帧。
    bool startContinuousDetection();
    void stopContinuousDetection();
    bool cancelDetection();

    // TCP 连接控制。
    bool connectTcpDevice();
    void disconnectTcpDevice();

    // 记录查询。
    QList<InspectionRecord> recentRecords(int limit = 8) const;
    bool lookupRecordByInspectionId(
        const QString &inspectionId,
        InspectionRecord *record,
        QString *errorMessage = nullptr) const;

    // 对外状态查询。
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
    bool isContinuousDetectionEnabled() const noexcept;
    int continuousDetectionIntervalMs() const noexcept;
    void setContinuousDetectionIntervalMs(int intervalMs);

signals:
    // 发给 UI 的状态广播信号。
    void statusChanged(const QString &message);
    void visionParamChanged();
    void deviceConfigChanged();
    void inputSourceConfigChanged();
    void captureStatusChanged(const CaptureStatusSnapshot &status);
    void previewFrameUpdated(const QImage &previewImage);
    void recordsChanged();
    void tcpStateChanged();
    // 发给采集 worker 的请求信号。
    void captureOpenRequested(const InputSourceConfig &config);
    void captureCloseRequested();
    void captureStartPreviewRequested();
    void captureStopPreviewRequested();
    // 发给检测 worker 的请求信号。
    void detectionRequested(const DetectionRequest &request);
    // 发给 UI 的检测状态信号。
    void detectionStarted();
    void detectionFinished(const DetectResult &result, const QImage &resultImage);
    void detectionFailed(const QString &errorMessage);
    void detectionCanceled();
    void detectionRunningChanged(bool isRunning);
    void continuousDetectionStateChanged(bool enabled);
    // 发给持久化和通信 worker 的请求信号。
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

    // 控制器状态与请求构建辅助。
    void updateStatus(const QString &message);
    bool buildFrameDetectionRequest(
        const CapturedFrame &frame,
        const QString &inspectionId,
        DetectionRequest *request,
        QString *errorMessage) const;
    // 图片模式入口：读取图片并转换为统一 CapturedFrame，再复用帧检测请求构建逻辑。
    bool buildFileDetectionRequest(
        const QString &imagePath,
        const QString &inspectionId,
        DetectionRequest *request,
        QString *errorMessage) const;
    // 采集 worker 回调：同步采集状态与预览帧。
    void handleCaptureStatusUpdated(const CaptureStatusSnapshot &status);
    void handlePreviewFrameReady(const CapturedFrame &frame);
    void renderLatestPreviewFrame();
    // 连续检测节拍回调：满足条件时对最新帧提交一次检测。
    void triggerContinuousDetectionTick();
    // 单次检测完成后的统一出口：更新状态并分发到 UI/存储/通信链路。
    void handleDetectionCompleted(const DetectionOutput &output);
    // worker 完成回调：分别处理检测失败、取消、持久化和通信结果。
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

    // 配置与服务入口。
    VisionParam m_visionParam;
    DeviceConfig m_deviceConfig;
    ConfigManager m_configManager;
    RecordManager m_recordManager;
    mutable LogManager m_logManager;

    // 后台 worker 线程与对象。
    QThread m_captureThread;
    CaptureWorker *m_captureWorker = nullptr;
    QThread m_detectionThread;
    DetectionWorker *m_detectionWorker = nullptr;
    QThread m_persistenceThread;
    InspectionPersistenceWorker *m_persistenceWorker = nullptr;
    QThread m_tcpThread;
    TcpWorker *m_tcpWorker = nullptr;

    // 输入源与预览帧上下文。
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
    QTimer *m_continuousDetectionTimer = nullptr;

    // 通信与检测流程状态。
    bool m_isTcpConnected = false;
    QString m_tcpStatusText = QStringLiteral("未连接");
    TcpOperationState m_tcpOperationState = TcpOperationState::Idle;
    bool m_isDetectionRunning = false;
    bool m_isDetectionCancelRequested = false;
    bool m_isContinuousDetectionEnabled = false;
    int m_continuousDetectionIntervalMs = 1000;
    qint64 m_lastContinuousDetectionFrameIndex = -1;
    QString m_activeInspectionId;
    QString m_lastCompletedInspectionId;
    QString m_statusMessage;
};
