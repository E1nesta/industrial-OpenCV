#pragma once

#include <QTcpSocket>
#include <QString>

#include <memory>

#include "models/deviceconfig.h"

class TcpManager
{
public:
    void setDeviceConfig(const DeviceConfig &config);
    DeviceConfig deviceConfig() const;
    bool hasValidConfig() const;
    bool connectToDevice(int timeoutMs = -1);
    void disconnectFromDevice(bool clearError = true);
    bool isConnected() const;
    bool sendResult(bool isOk, int timeoutMs = -1);
    QString lastError() const;
    QString lastReply() const;
    QString peerDescription() const;
    QString statusText() const;

private:
    int resolvedConnectTimeoutMs(int timeoutMs) const;
    int resolvedSendTimeoutMs(int timeoutMs) const;
    int resolvedSendRetryCount() const;
    QTcpSocket *socketIfAvailable() const;
    QTcpSocket &ensureSocket();
    bool sendPayloadWithReceipt(const QByteArray &payload, int timeoutMs);

    DeviceConfig m_deviceConfig;
    mutable std::unique_ptr<QTcpSocket> m_socket;
    QString m_lastError;
    QString m_lastReply;
};
