#include "communication/tcpmanager.h"

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

    if (host.isEmpty() || m_deviceConfig.port <= 0) {
        m_lastError = QStringLiteral("TCP 配置无效，请检查 IP 和端口。");
        return false;
    }

    if (isConnected()) {
        return true;
    }

    m_socket.abort();
    m_socket.connectToHost(host, static_cast<quint16>(m_deviceConfig.port));
    if (!m_socket.waitForConnected(timeoutMs)) {
        m_lastError = m_socket.errorString();
        return false;
    }

    m_lastError.clear();
    m_lastReply.clear();
    return true;
}

void TcpManager::disconnectFromDevice(bool clearError)
{
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.disconnectFromHost();
        if (m_socket.state() != QAbstractSocket::UnconnectedState) {
            m_socket.waitForDisconnected(300);
        }
    }

    if (clearError) {
        m_lastError.clear();
    }

    m_lastReply.clear();
}

bool TcpManager::isConnected() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

bool TcpManager::sendResult(bool isOk, int timeoutMs)
{
    m_lastError.clear();
    m_lastReply.clear();
    const QByteArray payload = buildResultPayload(isOk);

    if (!connectToDevice(timeoutMs)) {
        return false;
    }

    if (sendPayloadWithReceipt(payload, timeoutMs)) {
        return true;
    }

    const QString firstError = m_lastError;
    disconnectFromDevice(false);

    if (!connectToDevice(timeoutMs)) {
        if (!firstError.isEmpty()) {
            m_lastError = QStringLiteral("%1；重连失败：%2").arg(firstError, m_socket.errorString());
        }

        return false;
    }

    if (sendPayloadWithReceipt(payload, timeoutMs)) {
        return true;
    }

    if (!firstError.isEmpty() && !m_lastError.isEmpty()) {
        m_lastError = QStringLiteral("%1；重试失败：%2").arg(firstError, m_lastError);
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

    if (m_socket.bytesAvailable() > 0) {
        m_socket.readAll();
    }

    const qint64 written = m_socket.write(payload);
    if (written != payload.size()) {
        m_lastError = QStringLiteral("TCP 发送长度异常。");
        disconnectFromDevice(false);
        return false;
    }

    if (!m_socket.waitForBytesWritten(timeoutMs)) {
        m_lastError = m_socket.errorString();
        disconnectFromDevice(false);
        return false;
    }

    if (!m_socket.waitForReadyRead(timeoutMs)) {
        m_lastError = QStringLiteral("结果已发送，但未收到回执。");
        disconnectFromDevice(false);
        return false;
    }

    const QByteArray replyBytes = m_socket.readAll().trimmed();
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
