#include "app/appcontroller.h"

#include <algorithm>
#include <QDateTime>
#include <QFileInfo>
#include <QMetaType>
#include <QTimer>
#include <QUuid>
#include <utility>

#include "camera/captureworker.h"
#include "common/utils.h"
#include "communication/tcpworker.h"
#include "vision/detectionworker.h"

namespace
{
constexpr int kPreviewRenderIntervalMs = 125;
constexpr int kPreviewMaxLongEdge = 960;
constexpr int kDefaultContinuousDetectionIntervalMs = 1000;
constexpr int kMinimumContinuousDetectionIntervalMs = 100;

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
} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_logManager(this)
    , m_captureWorker(new CaptureWorker())
    , m_detectionWorker(new DetectionWorker(&m_logManager))
    , m_persistenceWorker(new InspectionPersistenceWorker())
    , m_tcpWorker(new TcpWorker())
{
    qRegisterMetaType<VisionParam>("VisionParam");
    qRegisterMetaType<InputSourceType>("InputSourceType");
    qRegisterMetaType<CaptureState>("CaptureState");
    qRegisterMetaType<InputSourceConfig>("InputSourceConfig");
    qRegisterMetaType<CaptureStatusSnapshot>("CaptureStatusSnapshot");
    qRegisterMetaType<FrameMeta>("FrameMeta");
    qRegisterMetaType<CapturedFrame>("CapturedFrame");
    qRegisterMetaType<DetectionRequest>("DetectionRequest");
    qRegisterMetaType<DetectResult>("DetectResult");
    qRegisterMetaType<DetectionOutput>("DetectionOutput");
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

    m_continuousDetectionTimer = new QTimer(this);
    m_continuousDetectionTimer->setInterval(kDefaultContinuousDetectionIntervalMs);
    connect(
        m_continuousDetectionTimer,
        &QTimer::timeout,
        this,
        &AppController::triggerContinuousDetectionTick);

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

    m_detectionWorker->moveToThread(&m_detectionThread);
    connect(&m_detectionThread, &QThread::finished, m_detectionWorker, &QObject::deleteLater);
    connect(this, &AppController::detectionRequested, m_detectionWorker, &DetectionWorker::process);
    connect(m_detectionWorker, &DetectionWorker::completed, this, &AppController::handleDetectionCompleted);
    connect(m_detectionWorker, &DetectionWorker::failed, this, &AppController::handleDetectionFailed);
    connect(m_detectionWorker, &DetectionWorker::canceled, this, &AppController::handleDetectionCanceled);
    m_detectionThread.start();

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
    m_detectionThread.quit();
    m_detectionThread.wait();
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
    m_visionParam = m_configManager.loadVisionParam();
    m_deviceConfig = m_configManager.loadDeviceConfig();
    m_inputSourceConfig = m_configManager.loadInputSourceConfig();
    m_captureStatus.source = m_inputSourceConfig;
    setContinuousDetectionIntervalMs(m_continuousDetectionIntervalMs);
    m_logManager.setMinimumLevelName(m_configManager.loadLogLevel());
    m_configManager.saveVisionParam(m_visionParam);
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
    emit visionParamChanged();
    emit deviceConfigChanged();
    emit inputSourceConfigChanged();
    emit captureStatusChanged(m_captureStatus);
    emit recordsChanged();
    emit tcpStateChanged();
}

void AppController::reloadConfig()
{
    // 运行态重载入口：从磁盘重读配置并同步到各 worker/界面。
    m_visionParam = m_configManager.loadVisionParam();
    m_deviceConfig = m_configManager.loadDeviceConfig();
    m_inputSourceConfig = m_configManager.loadInputSourceConfig();
    if (!m_captureStatus.opened) {
        m_captureStatus.source = m_inputSourceConfig;
    }
    m_logManager.setMinimumLevelName(m_configManager.loadLogLevel());
    emit tcpConfigRequested(m_deviceConfig, false);
    m_logManager.info(QStringLiteral("配置"), QStringLiteral("已从磁盘重新加载参数配置。"));
    updateStatus(QStringLiteral("已重新加载参数配置。"));
    emit visionParamChanged();
    emit deviceConfigChanged();
    emit inputSourceConfigChanged();
}

void AppController::saveCurrentParam()
{
    // 配置保存出口：把当前内存参数一次性落盘，避免不同配置项保存时序不一致。
    m_configManager.saveVisionParam(m_visionParam);
    m_configManager.saveDeviceConfig(m_deviceConfig);
    m_configManager.saveInputSourceConfig(m_inputSourceConfig);
    m_configManager.saveLogLevel(m_logManager.minimumLevelName());
    m_logManager.info(QStringLiteral("配置"), QStringLiteral("当前参数与通信配置已保存到磁盘。"));
    updateStatus(QStringLiteral("配置已保存到：%1").arg(configFilePath()));
}

void AppController::resetToDefaults()
{
    // 重置入口：先收敛运行态，再恢复默认配置并广播全量状态。
    stopContinuousDetection();
    if (m_captureStatus.opened || m_captureStatus.state == CaptureState::Previewing
        || m_captureStatus.state == CaptureState::Opening) {
        emit captureCloseRequested();
    }

    m_visionParam = VisionParam{};
    m_deviceConfig = DeviceConfig{};
    m_inputSourceConfig = InputSourceConfig{};
    m_inputSourceConfig.sourceName = QStringLiteral("camera-0");
    m_captureStatus.state = CaptureState::Idle;
    m_captureStatus.opened = false;
    m_captureStatus.source = m_inputSourceConfig;
    m_captureStatus.statusText = QStringLiteral("输入源未打开");
    m_captureStatus.lastFrameIndex = -1;
    m_latestFrame = CapturedFrame{};
    m_logManager.setMinimumLevelName(QStringLiteral("INFO"));
    // 默认通信配置生效时要求 TCP worker 先断连再应用。
    emit tcpConfigRequested(m_deviceConfig, true);
    m_configManager.saveVisionParam(m_visionParam);
    m_configManager.saveDeviceConfig(m_deviceConfig);
    m_configManager.saveInputSourceConfig(m_inputSourceConfig);
    m_configManager.saveLogLevel(m_logManager.minimumLevelName());
    m_logManager.info(QStringLiteral("配置"), QStringLiteral("参数与通信配置已恢复默认值。"));
    updateStatus(QStringLiteral("配置已重置为默认值。"));
    emit visionParamChanged();
    emit deviceConfigChanged();
    emit inputSourceConfigChanged();
    emit captureStatusChanged(m_captureStatus);
}

void AppController::setVisionParam(const VisionParam &param)
{
    m_visionParam = param;
    emit visionParamChanged();
}

void AppController::setDeviceConfig(const DeviceConfig &config)
{
    m_deviceConfig = config;
    // 设备配置变化后立即同步到 TCP worker，保持运行态一致。
    emit tcpConfigRequested(m_deviceConfig, false);
    emit deviceConfigChanged();
}

void AppController::setInputSourceConfig(const InputSourceConfig &config)
{
    m_inputSourceConfig = config;
    if (m_inputSourceConfig.type == InputSourceType::Camera
        && m_inputSourceConfig.sourceName.trimmed().isEmpty()) {
        m_inputSourceConfig.sourceName = QStringLiteral("camera-%1").arg(m_inputSourceConfig.deviceIndex);
    }

    if (!m_captureStatus.opened) {
        m_captureStatus.source = m_inputSourceConfig;
    }

    emit inputSourceConfigChanged();
}

bool AppController::startDetection(const QString &imagePath)
{
    // 图片检测入口：校验请求 -> 构建统一请求 -> 切换检测态 -> 分发到检测 worker。
    if (m_isDetectionRunning) {
        m_logManager.warn(QStringLiteral("检测"), QStringLiteral("检测任务仍在执行中。"));
        updateStatus(
            m_isDetectionCancelRequested ? QStringLiteral("正在取消上一个检测任务，请稍后再试。")
                                         : QStringLiteral("检测进行中，请等待当前任务完成。"));
        return false;
    }

    if (imagePath.isEmpty()) {
        m_logManager.warn(QStringLiteral("检测"), QStringLiteral("未提供待检测图片路径。"));
        updateStatus(QStringLiteral("检测失败：请先导入一张图片。"));
        return false;
    }

    m_activeInspectionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    DetectionRequest request;
    QString requestError;
    if (!buildFileDetectionRequest(imagePath, m_activeInspectionId, &request, &requestError)) {
        m_logManager.warn(
            QStringLiteral("检测"),
            QStringLiteral("无法创建检测请求：id=%1 %2").arg(m_activeInspectionId, requestError));
        updateStatus(QStringLiteral("检测失败：%1").arg(requestError));
        m_activeInspectionId.clear();
        return false;
    }

    m_detectionWorker->resetCancellation();
    m_isDetectionRunning = true;
    m_isDetectionCancelRequested = false;
    m_logManager.info(
        QStringLiteral("检测"),
        QStringLiteral("检测任务已提交：id=%1 source=%2").arg(m_activeInspectionId, imagePath));
    updateStatus(QStringLiteral("检测任务已提交，编号：%1").arg(m_activeInspectionId));
    // 先广播“检测中”状态给 UI，再把请求分发到检测 worker。
    emit detectionStarted();
    emit detectionRunningChanged(true);
    emit detectionRequested(request);
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
    stopContinuousDetection();
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
    m_captureStatus.statusText =
        QStringLiteral("正在预览：%1").arg(describeInputSource(m_captureStatus.source));
    m_previewFrameObserved = false;
    m_previewFrameDelivered = false;
    m_lastRenderedPreviewFrameIndex = -1;
    m_previewReceivedCount = 0;
    m_previewRenderedCount = 0;
    m_previewOverwrittenCount = 0;
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
    stopContinuousDetection();
    m_captureStatus.state = CaptureState::Idle;
    m_captureStatus.statusText =
        m_captureStatus.opened ? QStringLiteral("正在停止预览。") : QStringLiteral("输入源未打开。");
    m_previewRenderTimer->stop();
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

bool AppController::detectCurrentFrame()
{
    // 当前帧检测入口：状态门控通过后复用统一检测链路。
    // 当前帧检测只在“预览中 + 有有效最新帧 + 无在途检测任务”时允许触发。
    if (m_isDetectionRunning) {
        m_logManager.warn(QStringLiteral("检测"), QStringLiteral("检测任务仍在执行中。"));
        updateStatus(QStringLiteral("检测进行中，请等待当前任务完成。"));
        return false;
    }

    if (m_captureStatus.state != CaptureState::Previewing) {
        m_logManager.warn(QStringLiteral("检测"), QStringLiteral("当前采集状态不允许启动帧检测。"));
        updateStatus(QStringLiteral("检测失败：请先启动预览并保持输入源处于预览状态。"));
        return false;
    }

    if (!hasLatestFrame()) {
        m_logManager.warn(QStringLiteral("检测"), QStringLiteral("当前没有可用于检测的采集帧。"));
        updateStatus(QStringLiteral("检测失败：当前没有可用帧，请先启动预览。"));
        return false;
    }

    m_activeInspectionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    DetectionRequest request;
    QString requestError;
    if (!buildFrameDetectionRequest(m_latestFrame, m_activeInspectionId, &request, &requestError)) {
        m_logManager.warn(
            QStringLiteral("检测"),
            QStringLiteral("无法创建采集帧检测请求：id=%1 %2").arg(m_activeInspectionId, requestError));
        updateStatus(QStringLiteral("检测失败：%1").arg(requestError));
        m_activeInspectionId.clear();
        return false;
    }

    m_detectionWorker->resetCancellation();
    m_isDetectionRunning = true;
    m_isDetectionCancelRequested = false;
    m_logManager.info(
        QStringLiteral("检测"),
        QStringLiteral("采集帧检测任务已提交：id=%1 source=%2 frameIndex=%3")
            .arg(m_activeInspectionId)
            .arg(request.frame.meta.sourceName)
            .arg(request.frame.meta.frameIndex));
    updateStatus(QStringLiteral("检测任务已提交，编号：%1").arg(m_activeInspectionId));
    // 当前帧检测和图片检测共用同一套检测信号链。
    emit detectionStarted();
    emit detectionRunningChanged(true);
    emit detectionRequested(request);
    return true;
}

bool AppController::startContinuousDetection()
{
    // 连续检测入口：开启节拍器后由 tick 回调决定每轮是否真正提交检测。
    // 具体是否真正执行检测由 triggerContinuousDetectionTick 的门控逻辑决定。
    if (m_inputSourceConfig.type == InputSourceType::FileImage) {
        m_logManager.warn(QStringLiteral("检测"), QStringLiteral("静态图片模式不支持连续检测。"));
        updateStatus(QStringLiteral("连续检测仅支持视频文件或摄像头预览。"));
        return false;
    }

    if (m_captureStatus.state != CaptureState::Previewing) {
        m_logManager.warn(QStringLiteral("检测"), QStringLiteral("当前采集状态不允许开启连续检测。"));
        updateStatus(QStringLiteral("开启连续检测失败：请先启动预览。"));
        return false;
    }

    if (!hasLatestFrame()) {
        m_logManager.warn(QStringLiteral("检测"), QStringLiteral("当前没有可用于连续检测的采集帧。"));
        updateStatus(QStringLiteral("开启连续检测失败：当前没有可用帧，请稍候再试。"));
        return false;
    }

    if (m_isContinuousDetectionEnabled) {
        updateStatus(QStringLiteral("连续检测已在运行。"));
        return true;
    }

    m_isContinuousDetectionEnabled = true;
    m_lastContinuousDetectionFrameIndex = -1;
    m_continuousDetectionTimer->start();
    m_logManager.info(
        QStringLiteral("检测"),
        QStringLiteral("连续检测已启动：interval=%1ms source=%2")
            .arg(m_continuousDetectionIntervalMs)
            .arg(describeInputSource(m_captureStatus.source)));
    updateStatus(QStringLiteral("连续检测已启动，间隔 %1 ms。").arg(m_continuousDetectionIntervalMs));
    emit continuousDetectionStateChanged(true);
    triggerContinuousDetectionTick();
    return true;
}

void AppController::stopContinuousDetection()
{
    // 连续检测停止入口：统一收敛开关、帧索引和定时器。
    if (!m_isContinuousDetectionEnabled) {
        return;
    }

    m_isContinuousDetectionEnabled = false;
    m_lastContinuousDetectionFrameIndex = -1;
    if (m_continuousDetectionTimer != nullptr) {
        m_continuousDetectionTimer->stop();
    }
    m_logManager.info(QStringLiteral("检测"), QStringLiteral("连续检测已停止。"));
    updateStatus(QStringLiteral("连续检测已停止。"));
    emit continuousDetectionStateChanged(false);
}

bool AppController::buildFrameDetectionRequest(
    const CapturedFrame &frame,
    const QString &inspectionId,
    DetectionRequest *request,
    QString *errorMessage) const
{
    // 把采集帧复制为独立请求对象，避免后续预览帧被覆盖导致检测输入变化。
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (request == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("检测请求输出指针为空。");
        }
        return false;
    }
    if (!frame.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("待检测帧无效。");
        }
        return false;
    }

    CapturedFrame requestFrame = frame;
    requestFrame.image = frame.image.clone();
    if (!requestFrame.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("待检测帧拷贝失败。");
        }
        return false;
    }

    request->inspectionId = inspectionId;
    request->frame = std::move(requestFrame);
    request->visionParam = m_visionParam;
    return true;
}

bool AppController::buildFileDetectionRequest(
    const QString &imagePath,
    const QString &inspectionId,
    DetectionRequest *request,
    QString *errorMessage) const
{
    // 文件检测请求入口：先把图片转换为 CapturedFrame，再走统一帧请求构建逻辑。
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (request == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("检测请求输出指针为空。");
        }
        return false;
    }

    // 图片模式统一转换为 CapturedFrame，再复用 buildFrameDetectionRequest，
    // 保持图片/视频/摄像头三种输入在检测侧走同一请求结构。
    const QImage image(imagePath);
    if (image.isNull()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法读取待检测图片：%1").arg(imagePath);
        }
        return false;
    }

    CapturedFrame frame;
    frame.meta.captureId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    frame.meta.sourceType = InputSourceType::FileImage;
    frame.meta.sourcePath = imagePath;
    frame.meta.sourceName = QFileInfo(imagePath).fileName();
    frame.meta.frameIndex = 0;
    frame.meta.capturedAt = QDateTime::currentDateTime();
    frame.image = utils::qImageToMat(image);
    if (!frame.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("待检测图片转换失败：%1").arg(imagePath);
        }
        return false;
    }

    return buildFrameDetectionRequest(frame, inspectionId, request, errorMessage);
}

bool AppController::cancelDetection()
{
    // 取消入口：先停止连续检测，再向检测 worker 提交取消请求。
    if (!m_isDetectionRunning) {
        updateStatus(QStringLiteral("当前没有可取消的检测任务。"));
        return false;
    }

    if (m_isDetectionCancelRequested) {
        updateStatus(QStringLiteral("已提交取消请求，等待后台任务结束。"));
        return false;
    }

    if (m_isContinuousDetectionEnabled) {
        stopContinuousDetection();
    }

    m_isDetectionCancelRequested = true;
    m_detectionWorker->requestCancel();
    m_logManager.warn(
        QStringLiteral("检测"),
        m_activeInspectionId.isEmpty() ? QStringLiteral("已请求取消当前检测任务。")
                                       : QStringLiteral("已请求取消当前检测任务：id=%1")
                                             .arg(m_activeInspectionId));
    updateStatus(
        m_activeInspectionId.isEmpty() ? QStringLiteral("正在取消检测任务，请稍候。")
                                       : QStringLiteral("正在取消检测任务，编号：%1").arg(m_activeInspectionId));
    emit detectionRunningChanged(true);
    return true;
}

bool AppController::isContinuousDetectionEnabled() const noexcept
{
    return m_isContinuousDetectionEnabled;
}

int AppController::continuousDetectionIntervalMs() const noexcept
{
    return m_continuousDetectionIntervalMs;
}

void AppController::setContinuousDetectionIntervalMs(int intervalMs)
{
    m_continuousDetectionIntervalMs = std::max(kMinimumContinuousDetectionIntervalMs, intervalMs);
    if (m_continuousDetectionTimer != nullptr) {
        m_continuousDetectionTimer->setInterval(m_continuousDetectionIntervalMs);
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
    return m_activeInspectionId;
}

QString AppController::lastCompletedInspectionId() const
{
    return m_lastCompletedInspectionId;
}

const VisionParam &AppController::visionParam() const noexcept
{
    return m_visionParam;
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
    return QStringLiteral("阶段 4 / 帧驱动采集主线（预览 + 单帧检测）");
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

bool AppController::isDetectionRunning() const noexcept
{
    return m_isDetectionRunning;
}

bool AppController::isDetectionCancelRequested() const noexcept
{
    return m_isDetectionCancelRequested;
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
    if (m_isContinuousDetectionEnabled && m_captureStatus.state != CaptureState::Previewing) {
        stopContinuousDetection();
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

void AppController::triggerContinuousDetectionTick()
{
    // 连续检测只在开启状态下工作。
    if (!m_isContinuousDetectionEnabled) {
        return;
    }

    // 同一时刻只保留一个活动检测任务，避免状态流复杂化。
    if (m_isDetectionRunning || m_isDetectionCancelRequested) {
        return;
    }

    // 预览中且存在最新帧时才允许触发。
    if (m_captureStatus.state != CaptureState::Previewing || !hasLatestFrame()) {
        return;
    }

    const qint64 frameIndex = m_latestFrame.meta.frameIndex;
    // 同一帧只检测一次，避免重复计算和重复输出。
    if (frameIndex >= 0 && frameIndex == m_lastContinuousDetectionFrameIndex) {
        return;
    }

    if (detectCurrentFrame()) {
        m_lastContinuousDetectionFrameIndex = frameIndex;
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
        utils::buildPreviewImage(m_latestPreviewFrame.image, m_visionParam, kPreviewMaxLongEdge);
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

void AppController::handleDetectionCompleted(const DetectionOutput &output)
{
    // 检测完成统一出口：收敛检测状态后按持久化/TCP/UI 顺序分发结果。
    const DetectResult &result = output.result;
    m_logManager.info(
        QStringLiteral("检测"),
        QStringLiteral("主线程收到检测完成：inspectionId=%1 captureId=%2")
            .arg(result.inspectionId)
            .arg(output.request.frame.meta.captureId),
        false);
    m_isDetectionRunning = false;
    m_isDetectionCancelRequested = false;
    m_lastCompletedInspectionId = result.inspectionId;
    m_activeInspectionId.clear();

    m_logManager.info(
        QStringLiteral("检测"),
        QStringLiteral("检测完成：id=%1 result=%2 defects=%3 time=%4 ms")
            .arg(result.inspectionId)
            .arg(utils::boolToResultText(result.isOk))
            .arg(result.defectCount)
            .arg(result.processTimeMs, 0, 'f', 2));
    m_logManager.info(
        QStringLiteral("检测"),
        QStringLiteral("检测结论：id=%1 %2").arg(result.inspectionId).arg(result.message));

    updateStatus(
        QStringLiteral("检测完成：%1，缺陷 %2 处，耗时 %3 ms")
            .arg(utils::boolToResultText(result.isOk))
            .arg(result.defectCount)
            .arg(result.processTimeMs, 0, 'f', 2));
    // 单次检测完成后统一分发到后续链路：
    // 1) 持久化 2) 可选 TCP 发送 3) 结果图分发到 UI。
    m_logManager.info(
        QStringLiteral("记录"),
        QStringLiteral("持久化任务已提交：inspectionId=%1 captureId=%2 source=%3")
            .arg(result.inspectionId)
            .arg(output.request.frame.meta.captureId)
            .arg(output.request.frame.meta.sourceName),
        false);
    // 检测输出在这里分发到持久化链路。
    emit persistenceRequested(output);
    if (m_isTcpConnected) {
        m_logManager.info(
            QStringLiteral("通信"),
            QStringLiteral("TCP 发送任务已提交：inspectionId=%1 result=%2 peer=%3")
                .arg(result.inspectionId)
                .arg(utils::boolToResultText(result.isOk))
                .arg(QStringLiteral("%1:%2").arg(m_deviceConfig.ip).arg(m_deviceConfig.port)),
            false);
        // TCP 已连接时，检测结果继续分发到通信链路。
        emit tcpSendRequested(result.inspectionId, result.isOk);
    }

    m_logManager.info(
        QStringLiteral("检测"),
        QStringLiteral("结果图主线程转换开始：inspectionId=%1 size=%2x%3 type=%4")
            .arg(result.inspectionId)
            .arg(output.annotatedImage.cols)
            .arg(output.annotatedImage.rows)
            .arg(output.annotatedImage.type()),
        false);
    const QImage resultImage = utils::matToQImage(output.annotatedImage);
    m_logManager.info(
        QStringLiteral("检测"),
        QStringLiteral("结果图主线程转换完成：inspectionId=%1 isNull=%2 size=%3x%4")
            .arg(result.inspectionId)
            .arg(resultImage.isNull() ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(resultImage.width())
            .arg(resultImage.height()),
        false);
    m_logManager.info(
        QStringLiteral("检测"),
        QStringLiteral("结果图即将分发到界面：inspectionId=%1").arg(result.inspectionId),
        false);
    // UI 层统一消费主线程中的 QImage，避免跨线程直接传递 cv::Mat 给界面。
    emit detectionFinished(result, resultImage);
    m_logManager.info(
        QStringLiteral("检测"),
        QStringLiteral("结果图已分发到界面：inspectionId=%1").arg(result.inspectionId),
        false);
    emit detectionRunningChanged(false);
    // 连续检测开启时，检测完成后立即安排下一轮节拍检查。
    if (m_isContinuousDetectionEnabled) {
        QTimer::singleShot(0, this, &AppController::triggerContinuousDetectionTick);
    }
}

void AppController::handlePersistenceCompleted(const PersistenceResult &result)
{
    // 持久化回调：记录归档结果并在成功时刷新最近记录列表。
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
        return;
    }

    m_logManager.warn(
        QStringLiteral("记录"),
        QStringLiteral("检测记录保存失败：id=%1 %2")
            .arg(result.record.inspectionId)
            .arg(result.recordError));
    if (!m_isDetectionRunning) {
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
    bool connected)
{
    // TCP 发送回调：更新连接快照并把发送结果回写到主状态文案。
    m_isTcpConnected = connected;
    m_tcpStatusText = statusText;

    if (success) {
        m_logManager.info(
            QStringLiteral("通信"),
            QStringLiteral("检测结果已异步发送：id=%1 peer=%2 reply=%3")
                .arg(inspectionId)
                .arg(peerDescription)
                .arg(reply));
        if (!m_isDetectionRunning) {
            updateStatus(QStringLiteral("检测结果已发送，TCP 回执：%1").arg(reply));
        }
    } else {
        m_logManager.warn(
            QStringLiteral("通信"),
            QStringLiteral("TCP 结果发送失败：id=%1 peer=%2 error=%3")
                .arg(inspectionId)
                .arg(peerDescription)
                .arg(error));
        if (!m_isDetectionRunning) {
            updateStatus(QStringLiteral("检测完成，但 TCP 结果发送失败：%1").arg(error));
        }
    }

    emit tcpStateChanged();
}

void AppController::handleTcpConnectCompleted(
    bool success,
    const QString &error,
    const QString &peerDescription,
    const QString &statusText,
    bool connected)
{
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

void AppController::handleTcpConfigApplied(const QString &statusText, bool connected)
{
    // 配置应用回调：同步连接快照并触发 UI 状态刷新。
    m_isTcpConnected = connected;
    m_tcpStatusText = statusText;
    // 配置应用可能改变连接展示状态，需要广播给 UI 同步。
    emit tcpStateChanged();
}

void AppController::handleTcpDisconnectCompleted(
    const QString &peerDescription,
    const QString &statusText,
    bool connected)
{
    // TCP 断连回调：退出断连操作态并广播最终未连接状态。
    m_tcpOperationState = TcpOperationState::Idle;
    m_isTcpConnected = connected;
    m_tcpStatusText = statusText;
    m_logManager.info(QStringLiteral("通信"), QStringLiteral("TCP 已异步断开：%1").arg(peerDescription));
    updateStatus(QStringLiteral("TCP 已断开。"));
    emit tcpStateChanged();
}

void AppController::handleDetectionFailed(const QString &inspectionId, const QString &errorMessage)
{
    // 检测失败出口：统一收敛检测态并回传失败结果给 UI。
    // 失败路径统一清理检测状态；连续检测开启时一并停止。
    const bool stopContinuous = m_isContinuousDetectionEnabled;
    m_isDetectionRunning = false;
    m_isDetectionCancelRequested = false;
    m_activeInspectionId.clear();
    if (stopContinuous) {
        stopContinuousDetection();
    }
    m_logManager.error(
        QStringLiteral("检测"),
        inspectionId.isEmpty() ? errorMessage
                               : QStringLiteral("id=%1 %2").arg(inspectionId, errorMessage));
    updateStatus(
        inspectionId.isEmpty() ? QStringLiteral("检测失败：%1").arg(errorMessage)
                               : QStringLiteral("检测失败：%1（编号：%2）").arg(errorMessage, inspectionId));
    // 先向 UI 报告失败，再关闭“检测中”状态。
    emit detectionFailed(errorMessage);
    emit detectionRunningChanged(false);
}

void AppController::handleDetectionCanceled(const QString &inspectionId)
{
    // 检测取消出口：沿用失败路径的状态收敛策略，保证状态机一致。
    // 取消路径与失败路径一致：先收敛状态，再广播“已取消”结果。
    const bool stopContinuous = m_isContinuousDetectionEnabled;
    m_isDetectionRunning = false;
    m_isDetectionCancelRequested = false;
    m_activeInspectionId.clear();
    if (stopContinuous) {
        stopContinuousDetection();
    }
    m_logManager.warn(
        QStringLiteral("检测"),
        inspectionId.isEmpty() ? QStringLiteral("检测任务已取消。")
                               : QStringLiteral("检测任务已取消：id=%1").arg(inspectionId));
    updateStatus(
        inspectionId.isEmpty() ? QStringLiteral("检测任务已取消。")
                               : QStringLiteral("检测任务已取消，编号：%1").arg(inspectionId));
    emit detectionCanceled();
    emit detectionRunningChanged(false);
}
