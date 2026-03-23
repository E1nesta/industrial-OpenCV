#include "communication/tcpworker.h"

TcpWorker::TcpWorker(QObject *parent)
    : QObject(parent)
{
}

void TcpWorker::setDeviceConfig(const DeviceConfig &config)
{
    // 线程内同步接口：仅更新配置，不触发连接行为。
    m_tcpManager.setDeviceConfig(config);
}

bool TcpWorker::connectToDevice(int timeoutMs)
{
    // 线程内同步连接入口，主要用于测试或同线程调用。
    return m_tcpManager.connectToDevice(timeoutMs);
}

void TcpWorker::disconnectFromDevice(bool clearError)
{
    // 线程内同步断连入口。
    m_tcpManager.disconnectFromDevice(clearError);
}

bool TcpWorker::isConnected() const
{
    return m_tcpManager.isConnected();
}

QString TcpWorker::statusText() const
{
    return m_tcpManager.statusText();
}

QString TcpWorker::lastError() const
{
    return m_tcpManager.lastError();
}

QString TcpWorker::lastReply() const
{
    return m_tcpManager.lastReply();
}

QString TcpWorker::peerDescription() const
{
    return m_tcpManager.peerDescription();
}

void TcpWorker::applyDeviceConfigAsync(const DeviceConfig &config, bool disconnectFirst)
{
    // 配置异步入口：必要时先断连，再应用新配置。
    if (disconnectFirst) {
        m_tcpManager.disconnectFromDevice();
    }

    m_tcpManager.setDeviceConfig(config);
    // 配置应用完成后回传当前连接态，供控制层刷新界面状态。
    emit deviceConfigApplied(m_tcpManager.statusText(), m_tcpManager.isConnected());
}

void TcpWorker::connectToDeviceAsync(const DeviceConfig &config)
{
    // 连接异步入口：连接完成后统一通过回调信号返回结果。
    m_tcpManager.setDeviceConfig(config);
    const bool success = m_tcpManager.connectToDevice();
    // 连接结果统一从 worker 回传，避免控制层直接依赖 TcpManager 细节。
    emit connectCompleted(
        success,
        m_tcpManager.lastError(),
        m_tcpManager.peerDescription(),
        m_tcpManager.statusText(),
        m_tcpManager.isConnected());
}

void TcpWorker::disconnectFromDeviceAsync()
{
    // 断连异步入口：回传断连后的最终连接快照。
    const QString peer = m_tcpManager.peerDescription();
    m_tcpManager.disconnectFromDevice();
    // 断连后广播最终状态，便于界面和日志同步更新。
    emit disconnectCompleted(peer, m_tcpManager.statusText(), m_tcpManager.isConnected());
}

void TcpWorker::sendResultAsync(const QString &inspectionId, bool isOk)
{
    // 发送异步入口：使用默认超时策略。
    sendResultAsyncWithTimeout(inspectionId, isOk, -1);
}

void TcpWorker::sendResultAsyncWithTimeout(const QString &inspectionId, bool isOk, int timeoutMs)
{
    // 发送执行点：结果通过 sendCompleted 回传给控制层后续处理。
    const bool success = m_tcpManager.sendResult(isOk, timeoutMs);
    // 发送完成后带回 reply/error，控制层据此记录通信结果。
    emit sendCompleted(
        inspectionId,
        success,
        m_tcpManager.lastReply(),
        m_tcpManager.lastError(),
        m_tcpManager.peerDescription(),
        m_tcpManager.statusText(),
        m_tcpManager.isConnected());
}
