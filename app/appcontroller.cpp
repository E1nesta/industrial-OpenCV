#include "app/appcontroller.h"

#include <QMetaType>
#include <QUuid>

#include "common/utils.h"
#include "communication/tcpworker.h"
#include "vision/detectionworker.h"

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_logManager(this)
    , m_detectionWorker(new DetectionWorker(&m_logManager))
    , m_persistenceWorker(new InspectionPersistenceWorker())
    , m_tcpWorker(new TcpWorker())
{
    qRegisterMetaType<VisionParam>("VisionParam");
    qRegisterMetaType<DetectResult>("DetectResult");
    qRegisterMetaType<InspectionRecord>("InspectionRecord");
    qRegisterMetaType<QImage>("QImage");
    qRegisterMetaType<DeviceConfig>("DeviceConfig");

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
    m_logManager.setMinimumLevelName(m_configManager.loadLogLevel());
    m_configManager.saveVisionParam(m_visionParam);
    m_configManager.saveDeviceConfig(m_deviceConfig);
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
    emit recordsChanged();
    emit tcpStateChanged();
}

void AppController::reloadConfig()
{
    m_visionParam = m_configManager.loadVisionParam();
    m_deviceConfig = m_configManager.loadDeviceConfig();
    m_logManager.setMinimumLevelName(m_configManager.loadLogLevel());
    emit tcpConfigRequested(m_deviceConfig, false);
    m_logManager.info(QStringLiteral("配置"), QStringLiteral("已从磁盘重新加载参数配置。"));
    updateStatus(QStringLiteral("已重新加载参数配置。"));
    emit visionParamChanged();
    emit deviceConfigChanged();
}

void AppController::saveCurrentParam()
{
    m_configManager.saveVisionParam(m_visionParam);
    m_configManager.saveDeviceConfig(m_deviceConfig);
    m_configManager.saveLogLevel(m_logManager.minimumLevelName());
    m_logManager.info(QStringLiteral("配置"), QStringLiteral("当前参数与通信配置已保存到磁盘。"));
    updateStatus(QStringLiteral("配置已保存到：%1").arg(configFilePath()));
}

void AppController::resetToDefaults()
{
    m_visionParam = VisionParam{};
    m_deviceConfig = DeviceConfig{};
    m_logManager.setMinimumLevelName(QStringLiteral("INFO"));
    emit tcpConfigRequested(m_deviceConfig, true);
    m_configManager.saveVisionParam(m_visionParam);
    m_configManager.saveDeviceConfig(m_deviceConfig);
    m_configManager.saveLogLevel(m_logManager.minimumLevelName());
    m_logManager.info(QStringLiteral("配置"), QStringLiteral("参数与通信配置已恢复默认值。"));
    updateStatus(QStringLiteral("配置已重置为默认值。"));
    emit visionParamChanged();
    emit deviceConfigChanged();
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
    m_detectionWorker->resetCancellation();
    m_isDetectionRunning = true;
    m_isDetectionCancelRequested = false;
    m_logManager.info(
        QStringLiteral("检测"),
        QStringLiteral("检测任务已提交：id=%1 path=%2").arg(m_activeInspectionId, imagePath));
    updateStatus(QStringLiteral("检测任务已提交，编号：%1").arg(m_activeInspectionId));
    emit detectionStarted();
    emit detectionRunningChanged(true);
    emit detectionRequested(m_activeInspectionId, imagePath, m_visionParam);
    return true;
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
    return QStringLiteral("阶段 3 / 检测留存闭环（支持归档）");
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

void AppController::handleDetectionCompleted(const DetectResult &result, const QImage &resultImage)
{
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
    emit persistenceRequested(result, resultImage, m_visionParam);
    if (m_isTcpConnected) {
        emit tcpSendRequested(result.inspectionId, result.isOk);
    }

    emit detectionFinished(result, resultImage);
    emit detectionRunningChanged(false);
}

void AppController::handlePersistenceCompleted(
    const InspectionRecord &record,
    bool archiveSucceeded,
    const QString &archiveMessage,
    bool recordSaved,
    const QString &recordError)
{
    if (archiveSucceeded) {
        m_logManager.info(
            QStringLiteral("记录"),
            QStringLiteral("检测图片已归档：id=%1 %2").arg(record.inspectionId).arg(archiveMessage),
            false);
    } else {
        m_logManager.warn(
            QStringLiteral("记录"),
            QStringLiteral("检测图片归档失败：id=%1 %2").arg(record.inspectionId).arg(archiveMessage),
            false);
    }

    if (recordSaved) {
        m_logManager.info(
            QStringLiteral("记录"),
            QStringLiteral("检测记录已写入 SQLite：id=%1 timestamp=%2")
                .arg(record.inspectionId)
                .arg(record.timestamp),
            false);
        emit recordsChanged();
        return;
    }

    m_logManager.warn(
        QStringLiteral("记录"),
        QStringLiteral("检测记录保存失败：id=%1 %2").arg(record.inspectionId).arg(recordError));
    if (!m_isDetectionRunning) {
        updateStatus(QStringLiteral("检测完成，但记录保存失败：%1").arg(recordError));
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

void AppController::handleDetectionFailed(const QString &errorMessage)
{
    const QString inspectionId = m_activeInspectionId;
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

void AppController::handleDetectionCanceled()
{
    const QString inspectionId = m_activeInspectionId;
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
