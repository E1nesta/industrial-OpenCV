// 基础设施通信：tcpworker.h 负责外部通信链路与结果上报。
// 本文件承接巡检结果出口，处理连接、发送与回执流程。
#pragma once

#include <QObject>
#include <QString>

#include "infrastructure/communication/tcpmanager.h"
#include "domain/entities/deviceconfig.h"

// TcpWorker 负责在工作线程内包装 TcpManager 的异步调用入口。
// 它把配置、连接、发送结果等操作转换为可分发的信号回调。
class TcpWorker : public QObject
{
    Q_OBJECT

public:
    explicit TcpWorker(QObject *parent = nullptr);

    // 同步包装接口：主要用于线程内直接调用。
    void setDeviceConfig(const DeviceConfig &config);
    bool connectToDevice(int timeoutMs = -1);
    void disconnectFromDevice(bool clearError = true);
    bool isConnected() const;
    QString statusText() const;
    QString lastError() const;
    QString lastReply() const;
    QString peerDescription() const;

signals:
    // 发给控制层的异步回调信号。
    void deviceConfigApplied(const QString &statusText, bool connected, const DeviceConfig &config);
    void connectCompleted(
        bool success,
        const QString &error,
        const QString &peerDescription,
        const QString &statusText,
        bool connected,
        const DeviceConfig &config);
    void disconnectCompleted(
        const QString &peerDescription,
        const QString &statusText,
        bool connected,
        const DeviceConfig &config);
    void sendCompleted(
        const QString &inspectionId,
        bool success,
        const QString &reply,
        const QString &error,
        const QString &peerDescription,
        const QString &statusText,
        bool connected,
        const DeviceConfig &config);

public slots:
    // 控制层异步入口：配置、连接、断开、发送结果。
    void applyDeviceConfigAsync(const DeviceConfig &config, bool disconnectFirst);
    void connectToDeviceAsync(const DeviceConfig &config);
    void disconnectFromDeviceAsync();
    void sendResultAsync(const QString &inspectionId, bool isOk, const DeviceConfig &config);

private:
    // 内部发送实现：支持显式超时参数。
    void sendResultAsyncWithTimeout(
        const QString &inspectionId,
        bool isOk,
        const DeviceConfig &config,
        int timeoutMs);

    TcpManager m_tcpManager;
};
