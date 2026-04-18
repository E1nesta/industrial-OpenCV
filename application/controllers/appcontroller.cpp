#include "application/controllers/appcontroller.h"

#include <algorithm>
#include <QFileInfo>
#include <QMetaType>
#include <QTimer>
#include <utility>

#include "infrastructure/capture/captureworker.h"
#include "common/utils/utils.h"
#include "infrastructure/communication/tcpworker.h"
#include "infrastructure/vision/inspectionworker.h"

namespace
{
constexpr int kPreviewRenderIntervalMs = 125;
constexpr int kPreviewMaxLongEdge = 960;
constexpr int kDefaultContinuousInspectionIntervalMs =
    InspectionSessionState::kDefaultContinuousInspectionIntervalMs;

QString describeInputSource(const InputSourceConfig &config)
{
    if (!config.sourceName.trimmed().isEmpty()) {
        return config.sourceName.trimmed();
    }

    switch (config.type) {
    case InputSourceType::VideoFile:
        return config.sourcePath.trimmed().isEmpty() ? QStringLiteral("视频文件")
                                                     : QFileInfo(config.sourcePath).fileName();
    case InputSourceType::Camera:
        return QStringLiteral("camera-%1").arg(config.deviceIndex);
    case InputSourceType::FileImage:
    default:
        return config.sourcePath.trimmed().isEmpty() ? QStringLiteral("静态图片")
                                                     : QFileInfo(config.sourcePath).fileName();
    }
}

void applyTcpDispatchSnapshot(
    InspectionTask *task,
    bool tcpConnected,
    const DeviceConfig &deviceConfig)
{
    if (task == nullptr) {
        return;
    }

    task->shouldSendTcpResult = tcpConnected;
    task->tcpDeviceConfig = deviceConfig;
}

bool isSameTcpEndpoint(const DeviceConfig &lhs, const DeviceConfig &rhs)
{
    return lhs.ip == rhs.ip && lhs.port == rhs.port;
}

bool shouldInvalidateTcpStateForEndpointChange(
    const DeviceConfig &previousConfig,
    const DeviceConfig &nextConfig)
{
    return !isSameTcpEndpoint(previousConfig, nextConfig);
}

bool canAdoptPendingInputSourceConfig(const CaptureStatusSnapshot &captureStatus)
{
    return !captureStatus.opened
        && captureStatus.state != CaptureState::Opening
        && captureStatus.state != CaptureState::Closing;
}

Recipe sanitizeRecipe(const Recipe &recipe)
{
    Recipe sanitized = recipe;
    sanitized.threshold = std::clamp(sanitized.threshold, 0, 255);
    sanitized.minArea = std::max(0, sanitized.minArea);
    if (sanitized.maxArea > 0 && sanitized.maxArea < sanitized.minArea) {
        sanitized.maxArea = sanitized.minArea;
    }
    sanitized.roi.setWidth(std::max(0, sanitized.roi.width()));
    sanitized.roi.setHeight(std::max(0, sanitized.roi.height()));
    sanitized.imageSavePath = sanitized.imageSavePath.trimmed();
    if (sanitized.imageSavePath.isEmpty()) {
        sanitized.imageSavePath = QStringLiteral("data/images");
    }
    return sanitized;
}

DeviceConfig sanitizeDeviceConfig(const DeviceConfig &config)
{
    DeviceConfig sanitized = config;
    sanitized.ip = sanitized.ip.trimmed();
    sanitized.port = (sanitized.port > 0 && sanitized.port <= 65535) ? sanitized.port : 0;
    sanitized.comName = sanitized.comName.trimmed();
    sanitized.tcpConnectTimeoutMs = std::max(100, sanitized.tcpConnectTimeoutMs);
    sanitized.tcpSendTimeoutMs = std::max(100, sanitized.tcpSendTimeoutMs);
    sanitized.tcpSendRetryCount = std::max(0, sanitized.tcpSendRetryCount);
    sanitized.baudRate = std::max(0, sanitized.baudRate);
    return sanitized;
}

InputSourceConfig sanitizeInputSourceConfig(const InputSourceConfig &config)
{
    InputSourceConfig sanitized = config;
    sanitized.sourcePath = sanitized.sourcePath.trimmed();
    sanitized.sourceName = sanitized.sourceName.trimmed();
    sanitized.deviceIndex = std::max(0, sanitized.deviceIndex);
    sanitized.previewIntervalMs = std::max(1, sanitized.previewIntervalMs);
    if (sanitized.type == InputSourceType::Camera && sanitized.sourceName.isEmpty()) {
        sanitized.sourceName = QStringLiteral("camera-%1").arg(sanitized.deviceIndex);
    }
    return sanitized;
}
} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_logManager(this)
    , m_captureWorker(new CaptureWorker())
    , m_inspectionWorker(new InspectionWorker(&m_logManager))
    , m_persistenceWorker(new InspectionPersistenceWorker())
    , m_tcpWorker(new TcpWorker())
{
    qRegisterMetaType<Recipe>("Recipe");
    qRegisterMetaType<InputSourceType>("InputSourceType");
    qRegisterMetaType<CaptureState>("CaptureState");
    qRegisterMetaType<InputSourceConfig>("InputSourceConfig");
    qRegisterMetaType<CaptureStatusSnapshot>("CaptureStatusSnapshot");
    qRegisterMetaType<FrameMeta>("FrameMeta");
    qRegisterMetaType<CapturedFrame>("CapturedFrame");
    qRegisterMetaType<InspectionTask>("InspectionTask");
    qRegisterMetaType<InspectionResult>("InspectionResult");
    qRegisterMetaType<InspectionOutput>("InspectionOutput");
    qRegisterMetaType<InspectionRecord>("InspectionRecord");
    qRegisterMetaType<PersistenceResult>("PersistenceResult");
    qRegisterMetaType<QImage>("QImage");
    qRegisterMetaType<DeviceConfig>("DeviceConfig");

    m_inputSourceConfig.sourceName = QStringLiteral("camera-0");
    m_captureStatus.state = CaptureState::Idle;
    m_captureStatus.opened = false;
    m_captureStatus.source = m_inputSourceConfig;
    m_captureStatus.statusText = QStringLiteral("输入源未打开");

    m_previewRenderTimer = new QTimer(this);
    m_previewRenderTimer->setInterval(kPreviewRenderIntervalMs);
    connect(m_previewRenderTimer, &QTimer::timeout, this, &AppController::renderLatestPreviewFrame);

    m_continuousInspectionTimer = new QTimer(this);
    m_continuousInspectionTimer->setInterval(kDefaultContinuousInspectionIntervalMs);
    connect(
        m_continuousInspectionTimer,
        &QTimer::timeout,
        this,
        &AppController::triggerContinuousInspectionTick);

    m_captureWorker->moveToThread(&m_captureThread);
    connect(&m_captureThread, &QThread::finished, m_captureWorker, &QObject::deleteLater);
    connect(this, &AppController::captureOpenRequested, m_captureWorker, &CaptureWorker::openInputSource);
    connect(this, &AppController::captureCloseRequested, m_captureWorker, &CaptureWorker::closeInputSource);
    connect(
        this,
        &AppController::captureStartPreviewRequested,
        m_captureWorker,
        &CaptureWorker::startPreview);
    connect(
        this,
        &AppController::captureStopPreviewRequested,
        m_captureWorker,
        &CaptureWorker::stopPreview);
    connect(
        m_captureWorker,
        &CaptureWorker::captureStatusUpdated,
        this,
        &AppController::handleCaptureStatusUpdated);
    connect(m_captureWorker, &CaptureWorker::previewFrameReady, this, &AppController::handlePreviewFrameReady);
    m_captureThread.start();

    m_inspectionWorker->moveToThread(&m_inspectionThread);
    connect(&m_inspectionThread, &QThread::finished, m_inspectionWorker, &QObject::deleteLater);
    connect(this, &AppController::inspectionRequested, m_inspectionWorker, &InspectionWorker::process);
    connect(m_inspectionWorker, &InspectionWorker::completed, this, &AppController::handleInspectionCompleted);
    connect(m_inspectionWorker, &InspectionWorker::failed, this, &AppController::handleInspectionFailed);
    connect(m_inspectionWorker, &InspectionWorker::canceled, this, &AppController::handleInspectionCanceled);
    m_inspectionThread.start();

    m_persistenceWorker->moveToThread(&m_persistenceThread);
    connect(&m_persistenceThread, &QThread::finished, m_persistenceWorker, &QObject::deleteLater);
    connect(this, &AppController::persistenceRequested, m_persistenceWorker, &InspectionPersistenceWorker::persist);
    connect(
        m_persistenceWorker,
        &InspectionPersistenceWorker::persistenceCompleted,
        this,
        &AppController::handlePersistenceCompleted);
    m_persistenceThread.start();

    m_tcpWorker->moveToThread(&m_tcpThread);
    connect(&m_tcpThread, &QThread::finished, m_tcpWorker, &QObject::deleteLater);
    connect(this, &AppController::tcpConfigRequested, m_tcpWorker, &TcpWorker::applyDeviceConfigAsync);
    connect(this, &AppController::tcpConnectRequested, m_tcpWorker, &TcpWorker::connectToDeviceAsync);
    connect(this, &AppController::tcpDisconnectRequested, m_tcpWorker, &TcpWorker::disconnectFromDeviceAsync);
    connect(this, &AppController::tcpSendRequested, m_tcpWorker, &TcpWorker::sendResultAsync);
    connect(m_tcpWorker, &TcpWorker::deviceConfigApplied, this, &AppController::handleTcpConfigApplied);
    connect(m_tcpWorker, &TcpWorker::connectCompleted, this, &AppController::handleTcpConnectCompleted);
    connect(m_tcpWorker, &TcpWorker::disconnectCompleted, this, &AppController::handleTcpDisconnectCompleted);
    connect(m_tcpWorker, &TcpWorker::sendCompleted, this, &AppController::handleTcpSendCompleted);
    m_tcpThread.start();
}

AppController::~AppController()
{
    if (m_captureWorker != nullptr && m_captureThread.isRunning()) {
        QMetaObject::invokeMethod(m_captureWorker, "closeInputSource", Qt::BlockingQueuedConnection);
    }
    m_captureThread.quit();
    m_captureThread.wait();
    m_tcpThread.quit();
    m_tcpThread.wait();
    m_persistenceThread.quit();
    m_persistenceThread.wait();
    m_inspectionThread.quit();
    m_inspectionThread.wait();
}

LogManager &AppController::logManager() noexcept
{
    return m_logManager;
}

const LogManager &AppController::logManager() const noexcept
{
    return m_logManager;
}

void AppController::initialize()
{
    // 初始化主流程：加载配置 -> 同步 worker -> 初始化记录库 -> 广播初始状态到 UI。
    m_recipe = m_configManager.loadRecipe();
    m_deviceConfig = m_configManager.loadDeviceConfig();
    m_inputSourceConfig = m_configManager.loadInputSourceConfig();
    m_captureStatus.source = m_inputSourceConfig;
    setContinuousInspectionIntervalMs(m_inspectionState.continuousInspectionIntervalMs);
    m_logManager.setMinimumLevelName(m_configManager.loadLogLevel());
    m_configManager.saveRecipe(m_recipe);
    m_configManager.saveDeviceConfig(m_deviceConfig);
    m_configManager.saveInputSourceConfig(m_inputSourceConfig);
    m_configManager.saveLogLevel(m_logManager.minimumLevelName());
    // 初始化后先把通信配置同步给 TCP worker。
    emit tcpConfigRequested(m_deviceConfig, false);

    QString databaseError;
    if (m_recordManager.initialize(&databaseError)) {
        m_logManager.info(
            QStringLiteral("记录"),
            QStringLiteral("检测记录数据库已初始化：%1").arg(databaseFilePath()),
            false);
    } else {
        m_logManager.warn(
            QStringLiteral("记录"),
            QStringLiteral("检测记录数据库初始化失败：%1").arg(databaseError));
    }

    m_logManager.info(QStringLiteral("应用"), QStringLiteral("系统初始化完成。"));
    updateStatus(QStringLiteral("系统已启动，配置文件：%1").arg(configFilePath()));
    // 启动完成后广播一次全量状态，驱动 UI 首次同步。
    emit recipeChanged();
    emit deviceConfigChanged();
    emit inputSourceConfigChanged();
    emit captureStatusChanged(m_captureStatus);
    emit recordsChanged();
    emit tcpStateChanged();
}

void AppController::reloadConfig()
{
    // 运行态重载入口：从磁盘重读配置并同步到各 worker/界面。
    m_recipe = m_configManager.loadRecipe();
    const DeviceConfig previousDeviceConfig = m_deviceConfig;
    m_deviceConfig = m_configManager.loadDeviceConfig();
    m_inputSourceConfig = m_configManager.loadInputSourceConfig();
    bool captureStatusSourceUpdated = false;
    if (shouldInvalidateTcpStateForEndpointChange(previousDeviceConfig, m_deviceConfig)) {
        m_tcpOperationState = TcpOperationState::Idle;
        m_isTcpConnected = false;
        m_tcpStatusText =
            QStringLiteral("未连接（当前目标 %1:%2）").arg(m_deviceConfig.ip).arg(m_deviceConfig.port);
    }
    if (canAdoptPendingInputSourceConfig(m_captureStatus)) {
        m_captureStatus.source = m_inputSourceConfig;
        captureStatusSourceUpdated = true;
    }
    m_logManager.setMinimumLevelName(m_configManager.loadLogLevel());
    emit tcpConfigRequested(m_deviceConfig, false);
    m_logManager.info(QStringLiteral("配置"), QStringLiteral("已从磁盘重新加载参数配置。"));
    updateStatus(QStringLiteral("已重新加载参数配置。"));
    if (m_captureStatus.opened
        && m_captureStatus.state == CaptureState::Previewing
        && m_latestPreviewFrame.isValid()) {
        m_lastRenderedPreviewFrameIndex = -1;
        renderLatestPreviewFrame();
    }
    emit recipeChanged();
    emit deviceConfigChanged();
    emit inputSourceConfigChanged();
    if (captureStatusSourceUpdated) {
        emit captureStatusChanged(m_captureStatus);
    }
    emit tcpStateChanged();
}

void AppController::saveCurrentParam()
{
    // 配置保存出口：把当前内存参数一次性落盘，避免不同配置项保存时序不一致。
    m_configManager.saveRecipe(m_recipe);
    m_configManager.saveDeviceConfig(m_deviceConfig);
    m_configManager.saveInputSourceConfig(m_inputSourceConfig);
    m_configManager.saveLogLevel(m_logManager.minimumLevelName());
    m_logManager.info(QStringLiteral("配置"), QStringLiteral("当前参数与通信配置已保存到磁盘。"));
    updateStatus(QStringLiteral("配置已保存到：%1").arg(configFilePath()));
}

void AppController::resetToDefaults()
{
    // 重置入口：先收敛运行态，再恢复默认配置并广播全量状态。
    stopContinuousInspection();
    const bool inspectionWasRunning = m_inspectionState.inspectionRunning;
    if (inspectionWasRunning && !m_inspectionState.inspectionCancelRequested) {
        m_inspectionState.requestCancel();
        m_inspectionWorker->requestCancel();
    }
    if (m_captureStatus.opened || m_captureStatus.state == CaptureState::Previewing
        || m_captureStatus.state == CaptureState::Opening) {
        emit captureCloseRequested();
    }

    const DeviceConfig previousDeviceConfig = m_deviceConfig;
    m_recipe = Recipe{};
    m_deviceConfig = DeviceConfig{};
    m_inputSourceConfig = InputSourceConfig{};
    m_inputSourceConfig.sourceName = QStringLiteral("camera-0");
    m_captureStatus.state = CaptureState::Idle;
    m_captureStatus.opened = false;
    m_captureStatus.source = m_inputSourceConfig;
    m_captureStatus.statusText = QStringLiteral("输入源未打开");
    m_captureStatus.lastFrameIndex = -1;
    m_latestFrame = CapturedFrame{};
    m_latestPreviewFrame = CapturedFrame{};
    m_inspectionState.resetToDefaultsPreservingInFlight();
    if (shouldInvalidateTcpStateForEndpointChange(previousDeviceConfig, m_deviceConfig)) {
        m_tcpOperationState = TcpOperationState::Idle;
        m_isTcpConnected = false;
        m_tcpStatusText =
            QStringLiteral("未连接（当前目标 %1:%2）").arg(m_deviceConfig.ip).arg(m_deviceConfig.port);
    }
    setContinuousInspectionIntervalMs(InspectionSessionState::kDefaultContinuousInspectionIntervalMs);
    m_logManager.setMinimumLevelName(QStringLiteral("INFO"));
    // 默认通信配置生效时要求 TCP worker 先断连再应用。
    emit tcpConfigRequested(m_deviceConfig, true);
    m_configManager.saveRecipe(m_recipe);
    m_configManager.saveDeviceConfig(m_deviceConfig);
    m_configManager.saveInputSourceConfig(m_inputSourceConfig);
    m_configManager.saveLogLevel(m_logManager.minimumLevelName());
    m_logManager.info(QStringLiteral("配置"), QStringLiteral("参数与通信配置已恢复默认值。"));
    updateStatus(QStringLiteral("配置已重置为默认值。"));
    emit recipeChanged();
    emit deviceConfigChanged();
    emit inputSourceConfigChanged();
    emit captureStatusChanged(m_captureStatus);
    emit tcpStateChanged();
    emit inspectionRunningChanged(m_inspectionState.inspectionRunning);
}

void AppController::setRecipe(const Recipe &param)
{
    m_recipe = sanitizeRecipe(param);
    emit recipeChanged();
    if (m_captureStatus.opened
        && m_captureStatus.state == CaptureState::Previewing
        && m_latestPreviewFrame.isValid()) {
        m_lastRenderedPreviewFrameIndex = -1;
        renderLatestPreviewFrame();
    }
}

void AppController::setDeviceConfig(const DeviceConfig &config)
{
    const DeviceConfig previousDeviceConfig = m_deviceConfig;
    m_deviceConfig = sanitizeDeviceConfig(config);
    if (shouldInvalidateTcpStateForEndpointChange(previousDeviceConfig, m_deviceConfig)) {
        m_tcpOperationState = TcpOperationState::Idle;
        m_isTcpConnected = false;
        m_tcpStatusText =
            QStringLiteral("未连接（当前目标 %1:%2）").arg(m_deviceConfig.ip).arg(m_deviceConfig.port);
    }
    emit tcpStateChanged();
    // 设备配置变化后立即同步到 TCP worker，保持运行态一致。
    emit tcpConfigRequested(m_deviceConfig, false);
    emit deviceConfigChanged();
}

void AppController::setInputSourceConfig(const InputSourceConfig &config)
{
    m_inputSourceConfig = sanitizeInputSourceConfig(config);

    bool captureStatusSourceUpdated = false;
    if (canAdoptPendingInputSourceConfig(m_captureStatus)) {
        m_captureStatus.source = m_inputSourceConfig;
        captureStatusSourceUpdated = true;
    }

    emit inputSourceConfigChanged();
    if (captureStatusSourceUpdated) {
        emit captureStatusChanged(m_captureStatus);
    }
}

bool AppController::startInspection(const QString &imagePath)
{
    // 图片巡检入口：统一交给 orchestrator 完成任务构建与运行态切换。
    InspectionTask task;
    QString requestError;
    if (!m_inspectionOrchestrator.startInspectionFromFile(
            imagePath, m_recipe, m_inspectionState, &task, &requestError)) {
        m_logManager.warn(
            QStringLiteral("巡检"),
            QStringLiteral("无法创建巡检任务：%1").arg(requestError));
        updateStatus(QStringLiteral("巡检失败：%1").arg(requestError));
        return false;
    }

    applyTcpDispatchSnapshot(&task, m_isTcpConnected, m_deviceConfig);
    m_inspectionWorker->resetCancellation();
    m_logManager.info(
        QStringLiteral("巡检"),
        QStringLiteral("巡检任务已提交：id=%1 source=%2")
            .arg(m_inspectionState.activeInspectionId, imagePath));
    updateStatus(QStringLiteral("巡检任务已提交，编号：%1").arg(m_inspectionState.activeInspectionId));
    emit inspectionStarted();
    emit inspectionRunningChanged(true);
    emit inspectionRequested(task);
    return true;
}

bool AppController::openInputSource()
{
    // 输入源打开入口：只负责状态切换与请求分发，实际打开动作在采集 worker 执行。
    if (m_inputSourceConfig.type == InputSourceType::FileImage) {
        m_logManager.warn(QStringLiteral("采集"), QStringLiteral("静态图片输入无需打开采集线程。"));
        updateStatus(QStringLiteral("静态图片模式无需打开输入源。"));
        return false;
    }

    if (m_inputSourceConfig.type == InputSourceType::VideoFile
        && m_inputSourceConfig.sourcePath.trimmed().isEmpty()) {
        m_logManager.warn(QStringLiteral("采集"), QStringLiteral("视频输入源路径为空。"));
        updateStatus(QStringLiteral("打开输入源失败：请先设置视频文件路径。"));
        return false;
    }

    m_captureStatus.state = CaptureState::Opening;
    m_captureStatus.opened = false;
    m_captureStatus.source = m_inputSourceConfig;
    m_captureStatus.statusText =
        QStringLiteral("正在打开输入源：%1").arg(describeInputSource(m_inputSourceConfig));
    m_previewFrameObserved = false;
    m_previewFrameDelivered = false;
    m_lastRenderedPreviewFrameIndex = -1;
    m_previewReceivedCount = 0;
    m_previewRenderedCount = 0;
    m_previewOverwrittenCount = 0;
    m_latestFrame = CapturedFrame{};
    m_latestPreviewFrame = CapturedFrame{};
    m_previewRenderTimer->stop();
    emit previewFrameUpdated(QImage{});
    emit captureStatusChanged(m_captureStatus);

    m_logManager.info(
        QStringLiteral("采集"),
        QStringLiteral("已提交输入源打开请求：%1").arg(describeInputSource(m_inputSourceConfig)));
    updateStatus(m_captureStatus.statusText);
    // 输入源打开由采集 worker 异步执行，控制器只负责广播请求。
    emit captureOpenRequested(m_inputSourceConfig);
    return true;
}

void AppController::closeInputSource()
{
    // 输入源关闭入口：先复位预览上下文，再把关闭请求分发到采集 worker。
    stopContinuousInspection();
    m_captureStatus.state = CaptureState::Closing;
    m_captureStatus.lastFrameIndex = -1;
    m_captureStatus.statusText = QStringLiteral("正在关闭输入源。");
    m_latestFrame = CapturedFrame{};
    m_latestPreviewFrame = CapturedFrame{};
    m_previewFrameObserved = false;
    m_previewFrameDelivered = false;
    m_lastRenderedPreviewFrameIndex = -1;
    m_previewRenderTimer->stop();
    emit previewFrameUpdated(QImage{});
    emit captureStatusChanged(m_captureStatus);
    m_logManager.info(QStringLiteral("采集"), QStringLiteral("已提交输入源关闭请求。"), false);
    updateStatus(m_captureStatus.statusText);
    emit captureCloseRequested();
}

bool AppController::startPreview()
{
    // 预览启动入口：切换到预览态并提交异步预览请求。
    if (!m_captureStatus.opened) {
        m_logManager.warn(QStringLiteral("采集"), QStringLiteral("输入源未打开，无法启动预览。"));
        updateStatus(QStringLiteral("启动预览失败：请先打开输入源。"));
        return false;
    }

    m_captureStatus.state = CaptureState::Previewing;
    m_captureStatus.lastFrameIndex = -1;
    m_captureStatus.statusText =
        QStringLiteral("正在预览：%1").arg(describeInputSource(m_captureStatus.source));
    m_previewFrameObserved = false;
    m_previewFrameDelivered = false;
    m_lastRenderedPreviewFrameIndex = -1;
    m_previewReceivedCount = 0;
    m_previewRenderedCount = 0;
    m_previewOverwrittenCount = 0;
    m_latestFrame = CapturedFrame{};
    m_latestPreviewFrame = CapturedFrame{};
    m_previewRenderTimer->start();
    emit captureStatusChanged(m_captureStatus);

    m_logManager.info(
        QStringLiteral("采集"),
        QStringLiteral("已提交预览启动请求：%1").arg(describeInputSource(m_captureStatus.source)),
        false);
    updateStatus(m_captureStatus.statusText);
    // 预览真正开始与否由采集 worker 后续回调 captureStatusUpdated 决定。
    emit captureStartPreviewRequested();
    return true;
}

void AppController::stopPreview()
{
    // 预览停止入口：停止预览节拍并保持输入源连接态不变。
    stopContinuousInspection();
    m_captureStatus.state = CaptureState::Idle;
    m_captureStatus.lastFrameIndex = -1;
    m_captureStatus.statusText =
        m_captureStatus.opened ? QStringLiteral("正在停止预览。") : QStringLiteral("输入源未打开。");
    m_previewRenderTimer->stop();
    m_latestFrame = CapturedFrame{};
    m_latestPreviewFrame = CapturedFrame{};
    emit captureStatusChanged(m_captureStatus);
    if (m_captureStatus.opened) {
        m_logManager.info(QStringLiteral("采集"), QStringLiteral("已提交预览停止请求。"), false);
        m_logManager.info(
            QStringLiteral("采集"),
            QStringLiteral("预览统计：received=%1 rendered=%2 overwritten=%3")
                .arg(m_previewReceivedCount)
                .arg(m_previewRenderedCount)
                .arg(m_previewOverwrittenCount),
            false);
        updateStatus(m_captureStatus.statusText);
    }
    emit captureStopPreviewRequested();
}

bool AppController::inspectCurrentFrame()
{
    // 当前帧巡检入口：只在预览态把最新帧转换为独立巡检任务。
    InspectionTask task;
    QString requestError;
    if (!m_inspectionOrchestrator.startInspectionFromFrame(
            m_latestFrame,
            m_captureStatus,
            m_recipe,
            m_inspectionState,
            &task,
            &requestError)) {
        m_logManager.warn(
            QStringLiteral("巡检"),
            QStringLiteral("无法创建采集帧巡检任务：%1").arg(requestError));
        updateStatus(QStringLiteral("巡检失败：%1").arg(requestError));
        return false;
    }

    applyTcpDispatchSnapshot(&task, m_isTcpConnected, m_deviceConfig);
    m_inspectionWorker->resetCancellation();
    m_logManager.info(
        QStringLiteral("巡检"),
        QStringLiteral("采集帧巡检任务已提交：id=%1 source=%2 frameIndex=%3")
            .arg(m_inspectionState.activeInspectionId)
            .arg(task.frame.meta.sourceName)
            .arg(task.frame.meta.frameIndex));
    updateStatus(QStringLiteral("巡检任务已提交，编号：%1").arg(m_inspectionState.activeInspectionId));
    emit inspectionStarted();
    emit inspectionRunningChanged(true);
    emit inspectionRequested(task);
    return true;
}

bool AppController::startContinuousInspection()
{
    // 连续巡检入口：状态校验和门控判断统一交给 orchestrator。
    QString errorMessage;
    if (!m_inspectionOrchestrator.canStartContinuousInspection(
            m_captureStatus, m_latestFrame, m_inspectionState, &errorMessage)) {
        m_logManager.warn(QStringLiteral("巡检"), errorMessage);
        updateStatus(errorMessage);
        return false;
    }

    if (m_inspectionState.continuousInspectionEnabled) {
        updateStatus(QStringLiteral("连续巡检已在运行。"));
        return true;
    }

    m_inspectionState.startContinuousInspection();
    m_continuousInspectionTimer->start();
    m_logManager.info(
        QStringLiteral("巡检"),
        QStringLiteral("连续巡检已启动：interval=%1ms source=%2")
            .arg(m_inspectionState.continuousInspectionIntervalMs)
            .arg(describeInputSource(m_captureStatus.source)));
    updateStatus(
        QStringLiteral("连续巡检已启动，间隔 %1 ms。")
            .arg(m_inspectionState.continuousInspectionIntervalMs));
    emit continuousInspectionStateChanged(true);
    triggerContinuousInspectionTick();
    return true;
}

void AppController::stopContinuousInspection()
{
    // 连续巡检停止入口：统一收敛开关、帧索引和定时器。
    if (!m_inspectionState.stopContinuousInspection()) {
        return;
    }

    if (m_continuousInspectionTimer != nullptr) {
        m_continuousInspectionTimer->stop();
    }
    m_logManager.info(QStringLiteral("巡检"), QStringLiteral("连续巡检已停止。"));
    updateStatus(QStringLiteral("连续巡检已停止。"));
    emit continuousInspectionStateChanged(false);
}

bool AppController::cancelInspection()
{
    // 取消入口：先停止连续巡检，再向 worker 提交取消请求。
    if (!m_inspectionState.inspectionRunning) {
        updateStatus(QStringLiteral("当前没有可取消的巡检任务。"));
        return false;
    }

    if (m_inspectionState.inspectionCancelRequested) {
        updateStatus(QStringLiteral("已提交取消请求，等待后台任务结束。"));
        return false;
    }

    if (m_inspectionState.continuousInspectionEnabled) {
        stopContinuousInspection();
    }

    m_inspectionState.requestCancel();
    m_inspectionWorker->requestCancel();
    m_logManager.warn(
        QStringLiteral("巡检"),
        m_inspectionState.activeInspectionId.isEmpty()
            ? QStringLiteral("已请求取消当前巡检任务。")
            : QStringLiteral("已请求取消当前巡检任务：id=%1")
                  .arg(m_inspectionState.activeInspectionId));
    updateStatus(
        m_inspectionState.activeInspectionId.isEmpty()
            ? QStringLiteral("正在取消巡检任务，请稍候。")
            : QStringLiteral("正在取消巡检任务，编号：%1")
                  .arg(m_inspectionState.activeInspectionId));
    emit inspectionRunningChanged(true);
    return true;
}

bool AppController::isContinuousInspectionEnabled() const noexcept
{
    return m_inspectionState.continuousInspectionEnabled;
}

int AppController::continuousInspectionIntervalMs() const noexcept
{
    return m_inspectionState.continuousInspectionIntervalMs;
}

void AppController::setContinuousInspectionIntervalMs(int intervalMs)
{
    m_inspectionState.setContinuousInspectionIntervalMs(intervalMs);
    if (m_continuousInspectionTimer != nullptr) {
        m_continuousInspectionTimer->setInterval(m_inspectionState.continuousInspectionIntervalMs);
    }
}

bool AppController::connectTcpDevice()
{
    // TCP 连接入口：完成配置校验后进入“连接中”状态并发起异步连接请求。
    if (m_tcpOperationState != TcpOperationState::Idle) {
        updateStatus(QStringLiteral("TCP 正在处理其他操作，请稍候。"));
        return false;
    }

    const QString host = m_deviceConfig.ip.trimmed();
    if (host.isEmpty() || m_deviceConfig.port <= 0) {
        const QString error = QStringLiteral("TCP 配置无效，请检查 IP 和端口。");
        m_logManager.warn(QStringLiteral("通信"), error);
        m_tcpStatusText = QStringLiteral("未连接（%1）").arg(error);
        updateStatus(error);
        emit tcpStateChanged();
        return false;
    }

    if (m_isTcpConnected) {
        updateStatus(QStringLiteral("TCP 已连接：%1:%2").arg(host).arg(m_deviceConfig.port));
        return true;
    }

    m_tcpOperationState = TcpOperationState::Connecting;
    m_isTcpConnected = false;
    m_tcpStatusText = QStringLiteral("连接中 %1:%2").arg(host).arg(m_deviceConfig.port);
    m_logManager.info(
        QStringLiteral("通信"),
        QStringLiteral("已提交 TCP 连接请求：%1:%2").arg(host).arg(m_deviceConfig.port));
    updateStatus(QStringLiteral("正在连接 TCP：%1:%2").arg(host).arg(m_deviceConfig.port));
    // 先广播“连接中”状态，再提交异步连接请求。
    emit tcpStateChanged();
    emit tcpConnectRequested(m_deviceConfig);
    return true;
}

void AppController::disconnectTcpDevice()
{
    // TCP 断连入口：切换到“断开中”状态并发起异步断连请求。
    if (m_tcpOperationState != TcpOperationState::Idle) {
        updateStatus(QStringLiteral("TCP 正在处理其他操作，请稍候。"));
        return;
    }

    if (!m_isTcpConnected) {
        updateStatus(QStringLiteral("TCP 当前未连接。"));
        return;
    }

    const QString peerDescription = QStringLiteral("%1:%2").arg(m_deviceConfig.ip).arg(m_deviceConfig.port);
    m_tcpOperationState = TcpOperationState::Disconnecting;
    m_isTcpConnected = false;
    m_tcpStatusText = QStringLiteral("断开中 %1").arg(peerDescription);
    m_logManager.info(
        QStringLiteral("通信"),
        QStringLiteral("已提交 TCP 断开请求：%1").arg(peerDescription));
    updateStatus(QStringLiteral("正在断开 TCP：%1").arg(peerDescription));
    // 先广播“断开中”状态，再提交异步断连请求。
    emit tcpStateChanged();
    emit tcpDisconnectRequested();
}

QList<InspectionRecord> AppController::recentRecords(int limit) const
{
    QString errorMessage;
    QList<InspectionRecord> records = m_recordManager.recentRecords(limit, &errorMessage);
    if (!errorMessage.isEmpty()) {
        m_logManager.warn(
            QStringLiteral("记录"),
            QStringLiteral("读取最近记录失败：%1").arg(errorMessage),
            false);
    }

    return records;
}

bool AppController::lookupRecordByInspectionId(
    const QString &inspectionId,
    InspectionRecord *record,
    QString *errorMessage) const
{
    const bool found = m_recordManager.lookupRecordByInspectionId(inspectionId, record, errorMessage);
    if (!found && errorMessage != nullptr && !errorMessage->isEmpty()) {
        m_logManager.warn(
            QStringLiteral("记录"),
            QStringLiteral("按检测编号查询记录失败：id=%1 %2").arg(inspectionId, *errorMessage),
            false);
    }

    return found;
}

QString AppController::activeInspectionId() const
{
    return m_inspectionState.activeInspectionId;
}

QString AppController::lastCompletedInspectionId() const
{
    return m_inspectionState.lastCompletedInspectionId;
}

const Recipe &AppController::recipe() const noexcept
{
    return m_recipe;
}

const DeviceConfig &AppController::deviceConfig() const noexcept
{
    return m_deviceConfig;
}

const InputSourceConfig &AppController::inputSourceConfig() const noexcept
{
    return m_inputSourceConfig;
}

const CaptureStatusSnapshot &AppController::captureStatus() const noexcept
{
    return m_captureStatus;
}

QString AppController::configFilePath() const
{
    return m_configManager.configFilePath();
}

QString AppController::databaseFilePath() const
{
    return m_recordManager.databaseFilePath();
}

QString AppController::projectStage() const
{
    return QStringLiteral("视觉检测闭环（输入、预览、检测、留痕、TCP 输出）");
}

QString AppController::statusMessage() const
{
    return m_statusMessage;
}

QString AppController::tcpStatusText() const
{
    return m_tcpStatusText;
}

bool AppController::isTcpConnected() const
{
    return m_isTcpConnected;
}

bool AppController::isTcpOperationPending() const noexcept
{
    return m_tcpOperationState != TcpOperationState::Idle;
}

bool AppController::hasLatestFrame() const noexcept
{
    return m_latestFrame.isValid();
}

bool AppController::isInspectionRunning() const noexcept
{
    return m_inspectionState.inspectionRunning;
}

bool AppController::isInspectionCancelRequested() const noexcept
{
    return m_inspectionState.inspectionCancelRequested;
}

void AppController::updateStatus(const QString &message)
{
    // 状态文本出口：去重后再广播，避免 UI 被重复状态刷新。
    if (m_statusMessage == message) {
        return;
    }

    m_statusMessage = message;
    emit statusChanged(m_statusMessage);
}

void AppController::handleCaptureStatusUpdated(const CaptureStatusSnapshot &status)
{
    // 采集回调入口：同步采集快照并在必要时收敛预览/连续检测状态。
    const CaptureState previousState = m_captureStatus.state;
    m_captureStatus = status;
    // 连续检测只在预览态有效，离开预览态时立即停止节拍。
    if (m_inspectionState.continuousInspectionEnabled
        && m_captureStatus.state != CaptureState::Previewing) {
        stopContinuousInspection();
    }
    // 错误态或关闭态统一清理预览缓存，避免界面显示过期帧。
    if (m_captureStatus.state == CaptureState::Error
        || (!m_captureStatus.opened && m_captureStatus.state != CaptureState::Opening)) {
        m_captureStatus.source = m_inputSourceConfig;
        m_captureStatus.lastFrameIndex = -1;
        m_latestFrame = CapturedFrame{};
        m_latestPreviewFrame = CapturedFrame{};
        m_previewFrameObserved = false;
        m_previewFrameDelivered = false;
        m_lastRenderedPreviewFrameIndex = -1;
        m_previewRenderTimer->stop();
        emit previewFrameUpdated(QImage{});
    }

    // 状态变化时统一输出结构化日志，便于回放采集状态流。
    if (previousState != m_captureStatus.state || !m_captureStatus.statusText.isEmpty()) {
        const QString stateText = [this]() {
            switch (m_captureStatus.state) {
            case CaptureState::Opening:
                return QStringLiteral("Opening");
            case CaptureState::Previewing:
                return QStringLiteral("Previewing");
            case CaptureState::Closing:
                return QStringLiteral("Closing");
            case CaptureState::Error:
                return QStringLiteral("Error");
            case CaptureState::Idle:
            default:
                return QStringLiteral("Idle");
            }
        }();

        const QString message =
            QStringLiteral("采集状态更新：state=%1 opened=%2 frameIndex=%3 status=%4")
                .arg(stateText)
                .arg(m_captureStatus.opened ? QStringLiteral("true") : QStringLiteral("false"))
                .arg(m_captureStatus.lastFrameIndex)
                .arg(m_captureStatus.statusText);

        if (m_captureStatus.state == CaptureState::Error) {
            m_logManager.warn(QStringLiteral("采集"), message);
        } else {
            m_logManager.info(QStringLiteral("采集"), message, false);
        }
    }

    // 采集状态文本变更后同步到控制器主状态。
    if (!m_captureStatus.statusText.isEmpty()) {
        updateStatus(m_captureStatus.statusText);
    }

    // 最终由控制器统一广播采集快照，保持 UI 状态源单一。
    emit captureStatusChanged(m_captureStatus);
}

void AppController::triggerContinuousInspectionTick()
{
    // 连续巡检的状态门控由 orchestrator 统一判断。
    if (!m_inspectionOrchestrator.shouldTriggerContinuousInspection(
            m_captureStatus, m_latestFrame, m_inspectionState)) {
        return;
    }

    if (inspectCurrentFrame()) {
        m_inspectionOrchestrator.markContinuousInspectionTriggered(
            m_latestFrame, m_inspectionState);
    }
}

void AppController::handlePreviewFrameReady(const CapturedFrame &frame)
{
    // 预览帧回调入口：仅更新“最新帧缓存”，渲染交给定时渲染函数统一处理。
    if (!m_captureStatus.opened || m_captureStatus.state != CaptureState::Previewing) {
        return;
    }

    if (!m_previewFrameObserved) {
        m_logManager.info(
            QStringLiteral("采集"),
            QStringLiteral("收到首帧：source=%1 frameIndex=%2 size=%3x%4 type=%5")
                .arg(frame.meta.sourceName)
                .arg(frame.meta.frameIndex)
                .arg(frame.image.cols)
                .arg(frame.image.rows)
                .arg(frame.image.type()),
            false);
    }

    m_latestFrame = frame;
    ++m_previewReceivedCount;
    if (m_latestPreviewFrame.isValid() && m_latestPreviewFrame.meta.frameIndex != frame.meta.frameIndex) {
        ++m_previewOverwrittenCount;
    }
    m_latestPreviewFrame = frame;
    m_captureStatus.lastFrameIndex = frame.meta.frameIndex;
    m_previewFrameObserved = true;
}

void AppController::renderLatestPreviewFrame()
{
    // 预览渲染出口：把最新帧转换为 UI 预览图并广播到界面。
    if (!m_captureStatus.opened || m_captureStatus.state != CaptureState::Previewing) {
        return;
    }

    if (!m_latestPreviewFrame.isValid()) {
        return;
    }

    if (m_lastRenderedPreviewFrameIndex == m_latestPreviewFrame.meta.frameIndex) {
        return;
    }

    if (!m_previewFrameDelivered) {
        m_logManager.info(
            QStringLiteral("采集"),
            QStringLiteral("首帧预览渲染开始：source=%1 frameIndex=%2")
                .arg(m_latestPreviewFrame.meta.sourceName)
                .arg(m_latestPreviewFrame.meta.frameIndex),
            false);
    }

    const QImage previewImage =
        utils::buildPreviewImage(m_latestPreviewFrame.image, m_recipe, kPreviewMaxLongEdge);
    if (previewImage.isNull()) {
        m_logManager.warn(
            QStringLiteral("采集"),
            QStringLiteral("预览图转换失败：source=%1 frameIndex=%2")
                .arg(m_latestPreviewFrame.meta.sourceName)
                .arg(m_latestPreviewFrame.meta.frameIndex),
            false);
        return;
    }

    if (!m_previewFrameDelivered) {
        m_logManager.info(
            QStringLiteral("采集"),
            QStringLiteral("首帧预览图已生成：source=%1 frameIndex=%2 size=%3x%4")
                .arg(m_latestPreviewFrame.meta.sourceName)
                .arg(m_latestPreviewFrame.meta.frameIndex)
                .arg(previewImage.width())
                .arg(previewImage.height()),
            false);
    }

    m_lastRenderedPreviewFrameIndex = m_latestPreviewFrame.meta.frameIndex;
    ++m_previewRenderedCount;
    // 预览图统一由控制器转发到 UI，界面不直接依赖采集 worker。
    emit previewFrameUpdated(previewImage);

    if (!m_previewFrameDelivered) {
        m_logManager.info(
            QStringLiteral("采集"),
            QStringLiteral("首帧预览已投递：source=%1 frameIndex=%2")
                .arg(m_latestPreviewFrame.meta.sourceName)
                .arg(m_latestPreviewFrame.meta.frameIndex),
            false);
        m_previewFrameDelivered = true;
    }
}

void AppController::handleInspectionCompleted(const InspectionOutput &output)
{
    // 检测完成后的状态收敛与结果出口由 state + dispatcher 负责。
    if (!m_inspectionState.matchesActiveInspection(output.result.inspectionId)) {
        m_logManager.warn(
            QStringLiteral("巡检"),
            QStringLiteral("忽略非活动巡检完成回调：id=%1 active=%2 running=%3")
                .arg(output.result.inspectionId)
                .arg(m_inspectionState.activeInspectionId)
                .arg(m_inspectionState.inspectionRunning ? QStringLiteral("true")
                                                        : QStringLiteral("false")),
            false);
        return;
    }

    m_logManager.info(
        QStringLiteral("巡检"),
        QStringLiteral("主线程收到巡检完成：inspectionId=%1 captureId=%2")
            .arg(output.result.inspectionId)
            .arg(output.request.frame.meta.captureId),
        false);
    m_inspectionState.completeInspection(output.result.inspectionId);

    const ResultDispatchOutcome dispatchOutcome = m_resultDispatcher.dispatch(
        output,
        &m_logManager,
        [this](const InspectionOutput &dispatchOutput) { emit persistenceRequested(dispatchOutput); },
        [this](const QString &inspectionId, bool isOk, const DeviceConfig &config) {
            emit tcpSendRequested(inspectionId, isOk, config);
        });

    updateStatus(dispatchOutcome.statusMessage);
    emit inspectionFinished(dispatchOutcome.result, dispatchOutcome.resultImage);
    emit inspectionRunningChanged(false);
    // 连续巡检仍然由定时器节拍驱动，不在完成回调内直接补触发。
}

void AppController::handlePersistenceCompleted(const PersistenceResult &result)
{
    // 持久化回调：记录归档结果并在成功时刷新最近记录列表。
    const bool isLatestCompletedInspection =
        !m_inspectionState.lastCompletedInspectionId.isEmpty()
        && result.record.inspectionId == m_inspectionState.lastCompletedInspectionId;

    if (result.archiveSucceeded) {
        m_logManager.info(
            QStringLiteral("记录"),
            QStringLiteral("检测图片已归档：id=%1 %2")
                .arg(result.record.inspectionId)
                .arg(result.archiveMessage),
            false);
    } else {
        m_logManager.warn(
            QStringLiteral("记录"),
            QStringLiteral("检测图片归档失败：id=%1 %2")
                .arg(result.record.inspectionId)
                .arg(result.archiveMessage),
            false);
    }

    if (result.recordSaved) {
        m_logManager.info(
            QStringLiteral("记录"),
            QStringLiteral("持久化完成：inspectionId=%1 captureId=%2 timestamp=%3")
                .arg(result.record.inspectionId)
                .arg(result.captureId)
                .arg(result.record.timestamp),
            false);
        // 记录成功保存后刷新 UI 最近记录列表。
        emit recordsChanged();
        if (!result.archiveSucceeded
            && !m_inspectionState.inspectionRunning
            && isLatestCompletedInspection) {
            updateStatus(QStringLiteral("检测完成，但图片归档失败：%1").arg(result.archiveMessage));
        }
        return;
    }

    m_logManager.warn(
        QStringLiteral("记录"),
        QStringLiteral("检测记录保存失败：id=%1 %2")
            .arg(result.record.inspectionId)
            .arg(result.recordError));
    if (!m_inspectionState.inspectionRunning && isLatestCompletedInspection) {
        updateStatus(QStringLiteral("检测完成，但记录保存失败：%1").arg(result.recordError));
    }
}

void AppController::handleTcpSendCompleted(
    const QString &inspectionId,
    bool success,
    const QString &reply,
    const QString &error,
    const QString &peerDescription,
    const QString &statusText,
    bool connected,
    const DeviceConfig &config)
{
    // TCP 发送回调：仅在发送目标仍匹配当前配置时刷新全局连接快照。
    const bool matchesCurrentEndpoint = isSameTcpEndpoint(config, m_deviceConfig);
    if (matchesCurrentEndpoint) {
        m_isTcpConnected = connected;
        m_tcpStatusText = statusText;
    } else {
        m_logManager.info(
            QStringLiteral("通信"),
            QStringLiteral("忽略旧发送回调对当前 TCP 状态的覆盖：inspectionId=%1 sendPeer=%2 currentPeer=%3:%4")
                .arg(inspectionId)
                .arg(peerDescription)
                .arg(m_deviceConfig.ip)
                .arg(m_deviceConfig.port),
            false);
    }

    const bool isLatestCompletedInspection =
        !m_inspectionState.lastCompletedInspectionId.isEmpty()
        && inspectionId == m_inspectionState.lastCompletedInspectionId;

    if (success) {
        m_logManager.info(
            QStringLiteral("通信"),
            QStringLiteral("检测结果已异步发送：id=%1 peer=%2 reply=%3")
                .arg(inspectionId)
                .arg(peerDescription)
                .arg(reply));
        if (!m_inspectionState.inspectionRunning && isLatestCompletedInspection) {
            updateStatus(QStringLiteral("巡检结果已发送，TCP 回执：%1").arg(reply));
        }
    } else {
        m_logManager.warn(
            QStringLiteral("通信"),
            QStringLiteral("TCP 结果发送失败：id=%1 peer=%2 error=%3")
                .arg(inspectionId)
                .arg(peerDescription)
                .arg(error));
        if (!m_inspectionState.inspectionRunning && isLatestCompletedInspection) {
            updateStatus(QStringLiteral("巡检完成，但 TCP 结果发送失败：%1").arg(error));
        }
    }

    if (matchesCurrentEndpoint) {
        emit tcpStateChanged();
    }
}

void AppController::handleTcpConnectCompleted(
    bool success,
    const QString &error,
    const QString &peerDescription,
    const QString &statusText,
    bool connected,
    const DeviceConfig &config)
{
    if (!isSameTcpEndpoint(config, m_deviceConfig)) {
        m_logManager.info(
            QStringLiteral("通信"),
            QStringLiteral("忽略过期 TCP 连接回调：peer=%1 currentPeer=%2:%3")
                .arg(peerDescription)
                .arg(m_deviceConfig.ip)
                .arg(m_deviceConfig.port),
            false);
        return;
    }

    // TCP 连接回调：退出连接操作态并发布最终连接状态。
    m_tcpOperationState = TcpOperationState::Idle;
    m_isTcpConnected = connected;
    m_tcpStatusText = statusText;

    if (success) {
        m_logManager.info(
            QStringLiteral("通信"),
            QStringLiteral("TCP 已异步连接：%1").arg(peerDescription));
        updateStatus(QStringLiteral("TCP 已连接：%1").arg(peerDescription));
    } else {
        m_logManager.warn(
            QStringLiteral("通信"),
            QStringLiteral("TCP 异步连接失败：%1").arg(error));
        updateStatus(QStringLiteral("TCP 连接失败：%1").arg(error));
    }

    emit tcpStateChanged();
}

void AppController::handleTcpConfigApplied(
    const QString &statusText,
    bool connected,
    const DeviceConfig &config)
{
    if (!isSameTcpEndpoint(config, m_deviceConfig)) {
        m_logManager.info(
            QStringLiteral("通信"),
            QStringLiteral("忽略过期 TCP 配置回调：appliedPeer=%1:%2 currentPeer=%3:%4")
                .arg(config.ip)
                .arg(config.port)
                .arg(m_deviceConfig.ip)
                .arg(m_deviceConfig.port),
            false);
        return;
    }

    // 配置应用回调：同步连接快照并触发 UI 状态刷新。
    m_isTcpConnected = connected;
    m_tcpStatusText = statusText;
    // 配置应用可能改变连接展示状态，需要广播给 UI 同步。
    emit tcpStateChanged();
}

void AppController::handleTcpDisconnectCompleted(
    const QString &peerDescription,
    const QString &statusText,
    bool connected,
    const DeviceConfig &config)
{
    if (!isSameTcpEndpoint(config, m_deviceConfig)) {
        m_logManager.info(
            QStringLiteral("通信"),
            QStringLiteral("忽略过期 TCP 断连回调：peer=%1 currentPeer=%2:%3")
                .arg(peerDescription)
                .arg(m_deviceConfig.ip)
                .arg(m_deviceConfig.port),
            false);
        return;
    }

    // TCP 断连回调：退出断连操作态并广播最终未连接状态。
    m_tcpOperationState = TcpOperationState::Idle;
    m_isTcpConnected = connected;
    m_tcpStatusText = statusText;
    m_logManager.info(QStringLiteral("通信"), QStringLiteral("TCP 已异步断开：%1").arg(peerDescription));
    updateStatus(QStringLiteral("TCP 已断开。"));
    emit tcpStateChanged();
}

void AppController::handleInspectionFailed(const QString &inspectionId, const QString &errorMessage)
{
    // 巡检失败出口：统一收敛运行态，必要时关闭连续巡检。
    if (!m_inspectionState.matchesActiveInspection(inspectionId)) {
        m_logManager.warn(
            QStringLiteral("巡检"),
            QStringLiteral("忽略非活动巡检失败回调：id=%1 active=%2 running=%3 error=%4")
                .arg(inspectionId)
                .arg(m_inspectionState.activeInspectionId)
                .arg(m_inspectionState.inspectionRunning ? QStringLiteral("true")
                                                        : QStringLiteral("false"))
                .arg(errorMessage),
            false);
        return;
    }

    const bool stopContinuous = m_inspectionState.continuousInspectionEnabled;
    m_inspectionState.abortInspection();
    if (stopContinuous) {
        stopContinuousInspection();
    }
    m_logManager.error(
        QStringLiteral("巡检"),
        inspectionId.isEmpty() ? errorMessage
                               : QStringLiteral("id=%1 %2").arg(inspectionId, errorMessage));
    updateStatus(
        inspectionId.isEmpty() ? QStringLiteral("巡检失败：%1").arg(errorMessage)
                               : QStringLiteral("巡检失败：%1（编号：%2）").arg(errorMessage, inspectionId));
    // 先向 UI 报告失败，再关闭“巡检中”状态。
    emit inspectionFailed(errorMessage);
    emit inspectionRunningChanged(false);
}

void AppController::handleInspectionCanceled(const QString &inspectionId)
{
    // 巡检取消出口：沿用失败路径的状态收敛策略，保证状态机一致。
    if (!m_inspectionState.matchesActiveInspection(inspectionId)) {
        m_logManager.warn(
            QStringLiteral("巡检"),
            QStringLiteral("忽略非活动巡检取消回调：id=%1 active=%2 running=%3")
                .arg(inspectionId)
                .arg(m_inspectionState.activeInspectionId)
                .arg(m_inspectionState.inspectionRunning ? QStringLiteral("true")
                                                        : QStringLiteral("false")),
            false);
        return;
    }

    const bool stopContinuous = m_inspectionState.continuousInspectionEnabled;
    m_inspectionState.abortInspection();
    if (stopContinuous) {
        stopContinuousInspection();
    }
    m_logManager.warn(
        QStringLiteral("巡检"),
        inspectionId.isEmpty() ? QStringLiteral("巡检任务已取消。")
                               : QStringLiteral("巡检任务已取消：id=%1").arg(inspectionId));
    updateStatus(
        inspectionId.isEmpty() ? QStringLiteral("巡检任务已取消。")
                               : QStringLiteral("巡检任务已取消，编号：%1").arg(inspectionId));
    emit inspectionCanceled();
    emit inspectionRunningChanged(false);
}
