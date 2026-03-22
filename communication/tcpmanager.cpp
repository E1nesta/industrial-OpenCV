#include "communication/tcpmanager.h"

#include <algorithm>

#include "common/utils.h"

namespace
{
QByteArray buildResultPayload(bool isOk)
{
    return QByteArray("RESULT:") + utils::boolToResultText(isOk).toUtf8() + '\n';
}

bool isAckReply(const QByteArray &reply)
{
    return reply == "ACK" || reply.startsWith("ACK:");
}
} // namespace

void TcpManager::setDeviceConfig(const DeviceConfig &config)
{
    if (isConnected() &&
        (m_deviceConfig.ip != config.ip || m_deviceConfig.port != config.port)) {
        disconnectFromDevice();
    }

    m_deviceConfig = config;
}

DeviceConfig TcpManager::deviceConfig() const
{
    return m_deviceConfig;
}

bool TcpManager::hasValidConfig() const
{
    return !m_deviceConfig.ip.trimmed().isEmpty() && m_deviceConfig.port > 0;
}

bool TcpManager::connectToDevice(int timeoutMs)
{
    m_lastError.clear();
    const QString host = m_deviceConfig.ip.trimmed();
    QTcpSocket &socket = ensureSocket();
    const int effectiveTimeoutMs = resolvedConnectTimeoutMs(timeoutMs);

    if (host.isEmpty() || m_deviceConfig.port <= 0) {
        m_lastError = QStringLiteral("TCP 配置无效，请检查 IP 和端口。");
        return false;
    }

    if (isConnected()) {
        return true;
    }

    socket.abort();
    socket.connectToHost(host, static_cast<quint16>(m_deviceConfig.port));
    if (!socket.waitForConnected(effectiveTimeoutMs)) {
        m_lastError = socket.errorString();
        return false;
    }

    m_lastError.clear();
    m_lastReply.clear();
    return true;
}

void TcpManager::disconnectFromDevice(bool clearError)
{
    QTcpSocket *socket = socketIfAvailable();
    if (socket != nullptr && socket->state() != QAbstractSocket::UnconnectedState) {
        socket->disconnectFromHost();
        if (socket->state() != QAbstractSocket::UnconnectedState) {
            socket->waitForDisconnected(300);
        }
    }

    if (clearError) {
        m_lastError.clear();
    }

    m_lastReply.clear();
}

bool TcpManager::isConnected() const
{
    const QTcpSocket *socket = socketIfAvailable();
    return socket != nullptr && socket->state() == QAbstractSocket::ConnectedState;
}

bool TcpManager::sendResult(bool isOk, int timeoutMs)
{
    m_lastError.clear();
    m_lastReply.clear();
    const QByteArray payload = buildResultPayload(isOk);
    const int connectTimeoutMs = resolvedConnectTimeoutMs(timeoutMs);
    const int sendTimeoutMs = resolvedSendTimeoutMs(timeoutMs);
    const int retryCount = resolvedSendRetryCount();

    if (!connectToDevice(connectTimeoutMs)) {
        return false;
    }

    if (sendPayloadWithReceipt(payload, sendTimeoutMs)) {
        return true;
    }

    const QString firstError = m_lastError;
    QString latestRetryError = firstError;

    for (int attempt = 0; attempt < retryCount; ++attempt) {
        disconnectFromDevice(false);

        if (!connectToDevice(connectTimeoutMs)) {
            latestRetryError = QStringLiteral("重连失败（第 %1 次重试）：%2")
                                   .arg(attempt + 1)
                                   .arg(m_lastError);
            continue;
        }

        if (sendPayloadWithReceipt(payload, sendTimeoutMs)) {
            return true;
        }

        latestRetryError = QStringLiteral("重试发送失败（第 %1 次重试）：%2")
                               .arg(attempt + 1)
                               .arg(m_lastError);
    }

    if (!firstError.isEmpty() && !latestRetryError.isEmpty() && latestRetryError != firstError) {
        m_lastError = QStringLiteral("%1；%2").arg(firstError, latestRetryError);
    } else if (!latestRetryError.isEmpty()) {
        m_lastError = latestRetryError;
    }

    return false;
}

QString TcpManager::lastError() const
{
    return m_lastError;
}

QString TcpManager::lastReply() const
{
    return m_lastReply;
}

QString TcpManager::peerDescription() const
{
    return QStringLiteral("%1:%2").arg(m_deviceConfig.ip).arg(m_deviceConfig.port);
}

QString TcpManager::statusText() const
{
    if (isConnected()) {
        if (!m_lastReply.isEmpty()) {
            return QStringLiteral("已连接 %1（最近回执：%2）").arg(peerDescription(), m_lastReply);
        }

        if (!m_lastError.isEmpty()) {
            return QStringLiteral("已连接 %1（最近异常：%2）").arg(peerDescription(), m_lastError);
        }

        return QStringLiteral("已连接 %1").arg(peerDescription());
    }

    if (!m_lastError.isEmpty()) {
        return QStringLiteral("未连接（%1）").arg(m_lastError);
    }

    return QStringLiteral("未连接");
}

bool TcpManager::sendPayloadWithReceipt(const QByteArray &payload, int timeoutMs)
{
    if (!isConnected()) {
        m_lastError = QStringLiteral("TCP 未连接。");
        return false;
    }

    QTcpSocket &socket = ensureSocket();

    if (socket.bytesAvailable() > 0) {
        socket.readAll();
    }

    const qint64 written = socket.write(payload);
    if (written != payload.size()) {
        m_lastError = QStringLiteral("TCP 发送长度异常。");
        disconnectFromDevice(false);
        return false;
    }

    if (!socket.waitForBytesWritten(timeoutMs)) {
        m_lastError = socket.errorString();
        disconnectFromDevice(false);
        return false;
    }

    if (!socket.waitForReadyRead(timeoutMs)) {
        m_lastError = QStringLiteral("结果已发送，但未收到回执。");
        disconnectFromDevice(false);
        return false;
    }

    const QByteArray replyBytes = socket.readAll().trimmed();
    if (replyBytes.isEmpty()) {
        m_lastError = QStringLiteral("结果已发送，但回执为空。");
        disconnectFromDevice(false);
        return false;
    }

    if (!isAckReply(replyBytes)) {
        m_lastError = QStringLiteral("结果已发送，但收到无效回执：%1").arg(QString::fromUtf8(replyBytes));
        disconnectFromDevice(false);
        return false;
    }

    m_lastReply = QString::fromUtf8(replyBytes);
    m_lastError.clear();
    return true;
}

QTcpSocket *TcpManager::socketIfAvailable() const
{
    return m_socket.get();
}

QTcpSocket &TcpManager::ensureSocket()
{
    if (!m_socket) {
        m_socket = std::make_unique<QTcpSocket>();
    }

    return *m_socket;
}

int TcpManager::resolvedConnectTimeoutMs(int timeoutMs) const
{
    if (timeoutMs > 0) {
        return timeoutMs;
    }

    return std::max(100, m_deviceConfig.tcpConnectTimeoutMs);
}

int TcpManager::resolvedSendTimeoutMs(int timeoutMs) const
{
    if (timeoutMs > 0) {
        return timeoutMs;
    }

    return std::max(100, m_deviceConfig.tcpSendTimeoutMs);
}

int TcpManager::resolvedSendRetryCount() const
{
    return std::max(0, m_deviceConfig.tcpSendRetryCount);
}
