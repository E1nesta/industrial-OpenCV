#pragma once

#include <QTcpSocket>
#include <QString>

#include "models/deviceconfig.h"

class TcpManager
{
public:
    void setDeviceConfig(const DeviceConfig &config);
    DeviceConfig deviceConfig() const;
    bool hasValidConfig() const;
    bool connectToDevice(int timeoutMs = 2000);
    void disconnectFromDevice(bool clearError = true);
    bool isConnected() const;
    bool sendResult(bool isOk, int timeoutMs = 2000);
    QString lastError() const;
    QString lastReply() const;
    QString peerDescription() const;
    QString statusText() const;

private:
    bool sendPayloadWithReceipt(const QByteArray &payload, int timeoutMs);

    DeviceConfig m_deviceConfig;
    QTcpSocket m_socket;
    QString m_lastError;
    QString m_lastReply;
};
