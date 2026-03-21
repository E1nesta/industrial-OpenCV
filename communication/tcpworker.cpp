#include "communication/tcpworker.h"

TcpWorker::TcpWorker(QObject *parent)
    : QObject(parent)
{
}

void TcpWorker::setDeviceConfig(const DeviceConfig &config)
{
    m_tcpManager.setDeviceConfig(config);
}

bool TcpWorker::connectToDevice(int timeoutMs)
{
    return m_tcpManager.connectToDevice(timeoutMs);
}

void TcpWorker::disconnectFromDevice(bool clearError)
{
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
    if (disconnectFirst) {
        m_tcpManager.disconnectFromDevice();
    }

    m_tcpManager.setDeviceConfig(config);
    emit deviceConfigApplied(m_tcpManager.statusText(), m_tcpManager.isConnected());
}

void TcpWorker::connectToDeviceAsync(const DeviceConfig &config)
{
    m_tcpManager.setDeviceConfig(config);
    const bool success = m_tcpManager.connectToDevice();
    emit connectCompleted(
        success,
        m_tcpManager.lastError(),
        m_tcpManager.peerDescription(),
        m_tcpManager.statusText(),
        m_tcpManager.isConnected());
}

void TcpWorker::disconnectFromDeviceAsync()
{
    const QString peer = m_tcpManager.peerDescription();
    m_tcpManager.disconnectFromDevice();
    emit disconnectCompleted(peer, m_tcpManager.statusText(), m_tcpManager.isConnected());
}

void TcpWorker::sendResultAsync(const QString &inspectionId, bool isOk)
{
    sendResultAsyncWithTimeout(inspectionId, isOk, -1);
}

void TcpWorker::sendResultAsyncWithTimeout(const QString &inspectionId, bool isOk, int timeoutMs)
{
    const bool success = m_tcpManager.sendResult(isOk, timeoutMs);
    emit sendCompleted(
        inspectionId,
        success,
        m_tcpManager.lastReply(),
        m_tcpManager.lastError(),
        m_tcpManager.peerDescription(),
        m_tcpManager.statusText(),
        m_tcpManager.isConnected());
}
