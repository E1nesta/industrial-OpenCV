#include "app/appcontroller.h"

#include <QMetaType>

#include "common/utils.h"
#include "vision/detectionworker.h"

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_detectionWorker(new DetectionWorker)
{
    qRegisterMetaType<VisionParam>("VisionParam");
    qRegisterMetaType<DetectResult>("DetectResult");

    m_detectionWorker->moveToThread(&m_detectionThread);
    connect(&m_detectionThread, &QThread::finished, m_detectionWorker, &QObject::deleteLater);
    connect(this, &AppController::detectionRequested, m_detectionWorker, &DetectionWorker::process);
    connect(m_detectionWorker, &DetectionWorker::completed, this, &AppController::handleDetectionCompleted);
    connect(m_detectionWorker, &DetectionWorker::failed, this, &AppController::handleDetectionFailed);
    connect(m_detectionWorker, &DetectionWorker::canceled, this, &AppController::handleDetectionCanceled);
    m_detectionThread.start();
}

AppController::~AppController()
{
    m_detectionThread.quit();
    m_detectionThread.wait();
}

void AppController::initialize()
{
    m_visionParam = m_configManager.loadVisionParam();
    m_deviceConfig = m_configManager.loadDeviceConfig();
    m_configManager.saveVisionParam(m_visionParam);
    m_configManager.saveDeviceConfig(m_deviceConfig);
    m_tcpManager.setDeviceConfig(m_deviceConfig);

    QString databaseError;
    if (m_recordManager.initialize(&databaseError)) {
        m_logManager.info(
            QStringLiteral("记录"),
            QStringLiteral("检测记录数据库已初始化：%1").arg(databaseFilePath()));
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
    m_tcpManager.setDeviceConfig(m_deviceConfig);
    m_logManager.info(QStringLiteral("配置"), QStringLiteral("已从磁盘重新加载参数配置。"));
    updateStatus(QStringLiteral("已重新加载参数配置。"));
    emit visionParamChanged();
    emit deviceConfigChanged();
    emit tcpStateChanged();
}

void AppController::saveCurrentParam()
{
    m_configManager.saveVisionParam(m_visionParam);
    m_configManager.saveDeviceConfig(m_deviceConfig);
    m_logManager.info(QStringLiteral("配置"), QStringLiteral("当前参数与通信配置已保存到磁盘。"));
    updateStatus(QStringLiteral("配置已保存到：%1").arg(configFilePath()));
}

void AppController::resetToDefaults()
{
    m_visionParam = VisionParam{};
    m_deviceConfig = DeviceConfig{};
    m_tcpManager.disconnectFromDevice();
    m_tcpManager.setDeviceConfig(m_deviceConfig);
    m_configManager.saveVisionParam(m_visionParam);
    m_configManager.saveDeviceConfig(m_deviceConfig);
    m_logManager.info(QStringLiteral("配置"), QStringLiteral("参数与通信配置已恢复默认值。"));
    updateStatus(QStringLiteral("配置已重置为默认值。"));
    emit visionParamChanged();
    emit deviceConfigChanged();
    emit tcpStateChanged();
}

void AppController::setVisionParam(const VisionParam &param)
{
    m_visionParam = param;
    emit visionParamChanged();
}

void AppController::setDeviceConfig(const DeviceConfig &config)
{
    m_deviceConfig = config;
    m_tcpManager.setDeviceConfig(m_deviceConfig);
    emit deviceConfigChanged();
    emit tcpStateChanged();
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

    m_detectionWorker->resetCancellation();
    m_isDetectionRunning = true;
    m_isDetectionCancelRequested = false;
    m_logManager.info(QStringLiteral("检测"), QStringLiteral("检测任务已提交到工作线程：%1").arg(imagePath));
    updateStatus(QStringLiteral("检测任务已提交，正在后台执行。"));
    emit detectionStarted();
    emit detectionRunningChanged(true);
    emit detectionRequested(imagePath, m_visionParam);
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
    m_logManager.warn(QStringLiteral("检测"), QStringLiteral("已请求取消当前检测任务。"));
    updateStatus(QStringLiteral("正在取消检测任务，请稍候。"));
    emit detectionRunningChanged(true);
    return true;
}

bool AppController::connectTcpDevice()
{
    m_tcpManager.setDeviceConfig(m_deviceConfig);
    const bool ok = m_tcpManager.connectToDevice();

    if (ok) {
        m_logManager.info(
            QStringLiteral("通信"),
            QStringLiteral("TCP 已连接：%1").arg(m_tcpManager.peerDescription()));
        updateStatus(QStringLiteral("TCP 已连接：%1").arg(m_tcpManager.peerDescription()));
    } else {
        m_logManager.warn(
            QStringLiteral("通信"),
            QStringLiteral("TCP 连接失败：%1").arg(m_tcpManager.lastError()));
        updateStatus(QStringLiteral("TCP 连接失败：%1").arg(m_tcpManager.lastError()));
    }

    emit tcpStateChanged();
    return ok;
}

void AppController::disconnectTcpDevice()
{
    const QString peer = m_tcpManager.peerDescription();
    m_tcpManager.disconnectFromDevice();
    m_logManager.info(QStringLiteral("通信"), QStringLiteral("TCP 已断开：%1").arg(peer));
    updateStatus(QStringLiteral("TCP 已断开。"));
    emit tcpStateChanged();
}

QList<InspectionRecord> AppController::recentRecords(int limit) const
{
    QString errorMessage;
    QList<InspectionRecord> records = m_recordManager.recentRecords(limit, &errorMessage);
    if (!errorMessage.isEmpty()) {
        m_logManager.warn(
            QStringLiteral("记录"),
            QStringLiteral("读取最近记录失败：%1").arg(errorMessage));
    }

    return records;
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
    return QStringLiteral("阶段 2 / 异步单图检测闭环（支持取消）");
}

QString AppController::statusMessage() const
{
    return m_statusMessage;
}

QString AppController::tcpStatusText() const
{
    return m_tcpManager.statusText();
}

bool AppController::isTcpConnected() const
{
    return m_tcpManager.isConnected();
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

    m_logManager.info(
        QStringLiteral("检测"),
        QStringLiteral("检测完成，result=%1, defects=%2, time=%3 ms")
            .arg(utils::boolToResultText(result.isOk))
            .arg(result.defectCount)
            .arg(result.processTimeMs, 0, 'f', 2));

    updateStatus(
        QStringLiteral("检测完成：%1，缺陷 %2 处，耗时 %3 ms")
            .arg(utils::boolToResultText(result.isOk))
            .arg(result.defectCount)
            .arg(result.processTimeMs, 0, 'f', 2));

    InspectionRecord record;
    record.timestamp = utils::currentTimestamp();
    record.batchNo = QStringLiteral("LOCAL");
    record.isOk = result.isOk;
    record.defectCount = result.defectCount;
    record.processTimeMs = result.processTimeMs;
    record.imagePath = result.imagePath;

    QString recordError;
    if (m_recordManager.saveRecord(record, &recordError)) {
        m_logManager.info(QStringLiteral("记录"), QStringLiteral("检测记录已写入 SQLite。"));
        emit recordsChanged();
    } else {
        m_logManager.warn(
            QStringLiteral("记录"),
            QStringLiteral("检测记录保存失败：%1").arg(recordError));
    }

    if (m_tcpManager.hasValidConfig()) {
        if (m_tcpManager.sendResult(result.isOk)) {
            m_logManager.info(
                QStringLiteral("通信"),
                QStringLiteral("已发送检测结果到 %1：%2，回执：%3")
                    .arg(m_tcpManager.peerDescription(),
                         utils::boolToResultText(result.isOk),
                         m_tcpManager.lastReply()));
            updateStatus(
                QStringLiteral("检测完成：%1，TCP 回执：%2")
                    .arg(utils::boolToResultText(result.isOk), m_tcpManager.lastReply()));
            emit tcpStateChanged();
        } else {
            m_logManager.warn(
                QStringLiteral("通信"),
                QStringLiteral("TCP 结果发送失败：%1").arg(m_tcpManager.lastError()));
            updateStatus(QStringLiteral("检测完成，但 TCP 结果发送失败：%1").arg(m_tcpManager.lastError()));
            emit tcpStateChanged();
        }
    }

    emit detectionFinished(result, resultImage);
    emit detectionRunningChanged(false);
}

void AppController::handleDetectionFailed(const QString &errorMessage)
{
    m_isDetectionRunning = false;
    m_isDetectionCancelRequested = false;
    m_logManager.error(QStringLiteral("检测"), errorMessage);
    updateStatus(QStringLiteral("检测失败：%1").arg(errorMessage));
    emit detectionFailed(errorMessage);
    emit detectionRunningChanged(false);
}

void AppController::handleDetectionCanceled()
{
    m_isDetectionRunning = false;
    m_isDetectionCancelRequested = false;
    m_logManager.warn(QStringLiteral("检测"), QStringLiteral("检测任务已取消。"));
    updateStatus(QStringLiteral("检测任务已取消。"));
    emit detectionCanceled();
    emit detectionRunningChanged(false);
}
