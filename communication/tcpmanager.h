#pragma once

#include <QTcpSocket>
#include <QString>

#include <memory>

#include "models/deviceconfig.h"

// TcpManager 负责轻量 TCP 客户端能力：
// 连接设备、发送检测结果、等待 ACK 回执并维护最近通信状态。
class TcpManager
{
public:
    // 配置与连接控制。
    void setDeviceConfig(const DeviceConfig &config);
    DeviceConfig deviceConfig() const;
    bool hasValidConfig() const;
    bool connectToDevice(int timeoutMs = -1);
    void disconnectFromDevice(bool clearError = true);
    bool isConnected() const;

    // 发送 RESULT:OK/NG，并在超时窗口内等待 ACK 或 ACK:<message>。
    bool sendResult(bool isOk, int timeoutMs = -1);

    // 最近一次通信状态查询。
    QString lastError() const;
    QString lastReply() const;
    QString peerDescription() const;
    QString statusText() const;

private:
    // 超时与重试策略解析。
    int resolvedConnectTimeoutMs(int timeoutMs) const;
    int resolvedSendTimeoutMs(int timeoutMs) const;
    int resolvedSendRetryCount() const;

    // socket 生命周期与发送细节。
    QTcpSocket *socketIfAvailable() const;
    QTcpSocket &ensureSocket();
    bool sendPayloadWithReceipt(const QByteArray &payload, int timeoutMs);

    // 配置与通信缓存。
    DeviceConfig m_deviceConfig;
    mutable std::unique_ptr<QTcpSocket> m_socket;
    QString m_lastError;
    QString m_lastReply;
};
