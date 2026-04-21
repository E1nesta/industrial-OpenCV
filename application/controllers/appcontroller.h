// 应用层控制：appcontroller.h 负责接收界面请求并协调业务流程。
// 本文件位于巡检主链路入口，负责状态收敛与结果回推。
#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QThread>

#include "application/inspectionexecutionpayload.h"
#include "application/inspectiondispatchcontext.h"
#include "application/orchestrators/inspectionorchestrator.h"
#include "application/orchestrators/resultdispatcher.h"
#include "application/state/inspectionsessionstate.h"
#include "common/logging/logmanager.h"
#include "domain/entities/capturedframe.h"
#include "domain/entities/inspectiontask.h"
#include "domain/entities/inspectionresult.h"
#include "domain/entities/deviceconfig.h"
#include "domain/entities/inputsource.h"
#include "domain/entities/inspectionrecord.h"
#include "domain/entities/persistenceresult.h"
#include "domain/entities/recipe.h"
#include "infrastructure/config/configmanager.h"
#include "infrastructure/storage/inspectionpersistenceworker.h"
#include "infrastructure/storage/recordmanager.h"

class InspectionWorker;
class CaptureWorker;
class TcpWorker;
class QTimer;

// AppController 是应用层外观：
// 负责串联采集、巡检、持久化和 TCP 通信，并对 UI 暴露统一状态。
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
    void setRecipe(const Recipe &param);
    void setDeviceConfig(const DeviceConfig &config);
    void setInputSourceConfig(const InputSourceConfig &config);

    // 巡检入口：图片单次巡检、当前帧巡检、连续巡检。
    bool startInspection(const QString &imagePath);
    bool openInputSource();
    void closeInputSource();
    bool startPreview();
    void stopPreview();
    // 仅对当前最新帧触发一次巡检，适用于视频/摄像头预览场景。
    bool inspectCurrentFrame();
    // 连续巡检采用“最新帧 + 单任务门控”策略，不排队补帧。
    bool startContinuousInspection();
    void stopContinuousInspection();
    bool cancelInspection();

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
    const Recipe &recipe() const noexcept;
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
    bool isInspectionRunning() const noexcept;
    bool isInspectionCancelRequested() const noexcept;
    bool isContinuousInspectionEnabled() const noexcept;
    int continuousInspectionIntervalMs() const noexcept;
    void setContinuousInspectionIntervalMs(int intervalMs);

signals:
    // 发给 UI 的状态广播信号。
    void statusChanged(const QString &message);
    void recipeChanged();
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
    // 发给巡检 worker 的请求信号。
    void inspectionRequested(const InspectionTask &request);
    // 发给 UI 的巡检状态信号。
    void inspectionStarted();
    void inspectionFinished(const InspectionResult &result, const QImage &resultImage);
    void inspectionFailed(const QString &errorMessage);
    void inspectionCanceled();
    void inspectionRunningChanged(bool isRunning);
    void continuousInspectionStateChanged(bool enabled);
    // 发给持久化 worker 的执行载荷。
    void persistenceRequested(const InspectionExecutionPayload &executionPayload);
    // 发给通信 worker 的配置、连接和发送请求。
    void tcpConfigRequested(const DeviceConfig &config, bool disconnectFirst);
    void tcpConnectRequested(const DeviceConfig &config);
    void tcpDisconnectRequested();
    void tcpSendRequested(const QString &inspectionId, bool isOk, const DeviceConfig &config);

private:
    enum class TcpOperationState
    {
        Idle,
        Connecting,
        Disconnecting
    };

    // 控制器状态与请求构建辅助。
    void updateStatus(const QString &message);
    // 采集 worker 回调：同步采集状态与预览帧。
    void handleCaptureStatusUpdated(const CaptureStatusSnapshot &status);
    void handlePreviewFrameReady(const CapturedFrame &frame);
    void renderLatestPreviewFrame();
    void submitInspectionTask(const InspectionTask &task, const QString &logMessage);
    InspectionDispatchContext withDispatchContext(const InspectionExecutionPayload &executionPayload) const;
    // 连续巡检节拍回调：满足条件时对最新帧提交一次巡检。
    void triggerContinuousInspectionTick();
    // 单次巡检完成后的统一出口：更新状态并分发到 UI/存储/通信链路。
    void handleInspectionCompleted(const InspectionExecutionPayload &executionPayload);
    // worker 完成回调：分别处理巡检失败、取消、持久化和通信结果。
    void handleInspectionFailed(const QString &inspectionId, const QString &errorMessage);
    void handleInspectionCanceled(const QString &inspectionId);
    void handlePersistenceCompleted(const PersistenceResult &result);
    void handleTcpSendCompleted(
        const QString &inspectionId,
        bool success,
        const QString &reply,
        const QString &error,
        const QString &peerDescription,
        const QString &statusText,
        bool connected,
        const DeviceConfig &config);
    void handleTcpConnectCompleted(
        bool success,
        const QString &error,
        const QString &peerDescription,
        const QString &statusText,
        bool connected,
        const DeviceConfig &config);
    void handleTcpConfigApplied(
        const QString &statusText,
        bool connected,
        const DeviceConfig &config);
    void handleTcpDisconnectCompleted(
        const QString &peerDescription,
        const QString &statusText,
        bool connected,
        const DeviceConfig &config);

    // 配置与服务入口。
    Recipe m_recipe;
    DeviceConfig m_deviceConfig;
    ConfigManager m_configManager;
    RecordManager m_recordManager;
    InspectionOrchestrator m_inspectionOrchestrator;
    ResultDispatcher m_resultDispatcher;
    mutable LogManager m_logManager;
    InspectionSessionState m_inspectionState;

    // 后台 worker 线程与对象。
    QThread m_captureThread;
    CaptureWorker *m_captureWorker = nullptr;
    QThread m_inspectionThread;
    InspectionWorker *m_inspectionWorker = nullptr;
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
    QTimer *m_continuousInspectionTimer = nullptr;

    // 通信流程状态。
    bool m_isTcpConnected = false;
    QString m_tcpStatusText = QStringLiteral("未连接");
    TcpOperationState m_tcpOperationState = TcpOperationState::Idle;
    QString m_statusMessage;
};
