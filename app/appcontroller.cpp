#include "app/appcontroller.h"

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
    m_visionParam = m_configManager.loadVisionParam();
    m_deviceConfig = m_configManager.loadDeviceConfig();
    m_inputSourceConfig = m_configManager.loadInputSourceConfig();
    m_captureStatus.source = m_inputSourceConfig;
    m_logManager.setMinimumLevelName(m_configManager.loadLogLevel());
    m_configManager.saveVisionParam(m_visionParam);
    m_configManager.saveDeviceConfig(m_deviceConfig);
    m_configManager.saveInputSourceConfig(m_inputSourceConfig);
    m_configManager.saveLogLevel(m_logManager.minimumLevelName());
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
    emit visionParamChanged();
    emit deviceConfigChanged();
    emit inputSourceConfigChanged();
    emit captureStatusChanged(m_captureStatus);
    emit recordsChanged();
    emit tcpStateChanged();
}

void AppController::reloadConfig()
{
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
    m_configManager.saveVisionParam(m_visionParam);
    m_configManager.saveDeviceConfig(m_deviceConfig);
    m_configManager.saveInputSourceConfig(m_inputSourceConfig);
    m_configManager.saveLogLevel(m_logManager.minimumLevelName());
    m_logManager.info(QStringLiteral("配置"), QStringLiteral("当前参数与通信配置已保存到磁盘。"));
    updateStatus(QStringLiteral("配置已保存到：%1").arg(configFilePath()));
}

void AppController::resetToDefaults()
{
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
    emit detectionStarted();
    emit detectionRunningChanged(true);
    emit detectionRequested(request);
    return true;
}

bool AppController::openInputSource()
{
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
    emit captureOpenRequested(m_inputSourceConfig);
    return true;
}

void AppController::closeInputSource()
{
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
    emit captureStartPreviewRequested();
    return true;
}

void AppController::stopPreview()
{
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
    if (m_isDetectionRunning) {
        m_logManager.warn(QStringLiteral("检测"), QStringLiteral("检测任务仍在执行中。"));
        updateStatus(QStringLiteral("检测进行中，请等待当前任务完成。"));
        return false;
    }

    if (m_captureStatus.state == CaptureState::Opening || m_captureStatus.state == CaptureState::Closing
        || m_captureStatus.state == CaptureState::Error) {
        m_logManager.warn(QStringLiteral("检测"), QStringLiteral("当前采集状态不允许启动帧检测。"));
        updateStatus(QStringLiteral("检测失败：当前采集状态不可用，请重新打开并启动预览。"));
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
    emit detectionStarted();
    emit detectionRunningChanged(true);
    emit detectionRequested(request);
    return true;
}

bool AppController::buildFrameDetectionRequest(
    const CapturedFrame &frame,
    const QString &inspectionId,
    DetectionRequest *request,
    QString *errorMessage) const
{
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
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (request == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("检测请求输出指针为空。");
        }
        return false;
    }

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
    if (!m_isDetectionRunning) {
        updateStatus(QStringLiteral("当前没有可取消的检测任务。"));
        return false;
    }

    if (m_isDetectionCancelRequested) {
        updateStatus(QStringLiteral("已提交取消请求，等待后台任务结束。"));
        return false;
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

bool AppController::connectTcpDevice()
{
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
    emit tcpStateChanged();
    emit tcpConnectRequested(m_deviceConfig);
    return true;
}

void AppController::disconnectTcpDevice()
{
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
    if (m_statusMessage == message) {
        return;
    }

    m_statusMessage = message;
    emit statusChanged(m_statusMessage);
}

void AppController::handleCaptureStatusUpdated(const CaptureStatusSnapshot &status)
{
    const CaptureState previousState = m_captureStatus.state;
    m_captureStatus = status;
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

    if (!m_captureStatus.statusText.isEmpty()) {
        updateStatus(m_captureStatus.statusText);
    }

    emit captureStatusChanged(m_captureStatus);
}

void AppController::handlePreviewFrameReady(const CapturedFrame &frame)
{
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
    m_logManager.info(
        QStringLiteral("记录"),
        QStringLiteral("持久化任务已提交：inspectionId=%1 captureId=%2 source=%3")
            .arg(result.inspectionId)
            .arg(output.request.frame.meta.captureId)
            .arg(output.request.frame.meta.sourceName),
        false);
    emit persistenceRequested(output);
    if (m_isTcpConnected) {
        m_logManager.info(
            QStringLiteral("通信"),
            QStringLiteral("TCP 发送任务已提交：inspectionId=%1 result=%2 peer=%3")
                .arg(result.inspectionId)
                .arg(utils::boolToResultText(result.isOk))
                .arg(QStringLiteral("%1:%2").arg(m_deviceConfig.ip).arg(m_deviceConfig.port)),
            false);
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
        QStringLiteral("结果图即将投递到界面：inspectionId=%1").arg(result.inspectionId),
        false);
    emit detectionFinished(result, resultImage);
    m_logManager.info(
        QStringLiteral("检测"),
        QStringLiteral("结果图已投递到界面：inspectionId=%1").arg(result.inspectionId),
        false);
    emit detectionRunningChanged(false);
}

void AppController::handlePersistenceCompleted(const PersistenceResult &result)
{
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
    m_isTcpConnected = connected;
    m_tcpStatusText = statusText;
    emit tcpStateChanged();
}

void AppController::handleTcpDisconnectCompleted(
    const QString &peerDescription,
    const QString &statusText,
    bool connected)
{
    m_tcpOperationState = TcpOperationState::Idle;
    m_isTcpConnected = connected;
    m_tcpStatusText = statusText;
    m_logManager.info(QStringLiteral("通信"), QStringLiteral("TCP 已异步断开：%1").arg(peerDescription));
    updateStatus(QStringLiteral("TCP 已断开。"));
    emit tcpStateChanged();
}

void AppController::handleDetectionFailed(const QString &inspectionId, const QString &errorMessage)
{
    m_isDetectionRunning = false;
    m_isDetectionCancelRequested = false;
    m_activeInspectionId.clear();
    m_logManager.error(
        QStringLiteral("检测"),
        inspectionId.isEmpty() ? errorMessage
                               : QStringLiteral("id=%1 %2").arg(inspectionId, errorMessage));
    updateStatus(
        inspectionId.isEmpty() ? QStringLiteral("检测失败：%1").arg(errorMessage)
                               : QStringLiteral("检测失败：%1（编号：%2）").arg(errorMessage, inspectionId));
    emit detectionFailed(errorMessage);
    emit detectionRunningChanged(false);
}

void AppController::handleDetectionCanceled(const QString &inspectionId)
{
    m_isDetectionRunning = false;
    m_isDetectionCancelRequested = false;
    m_activeInspectionId.clear();
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
