#pragma once

#include <QObject>
#include <QString>

#include "communication/tcpmanager.h"
#include "models/deviceconfig.h"

class TcpWorker : public QObject
{
    Q_OBJECT

public:
    explicit TcpWorker(QObject *parent = nullptr);

    void setDeviceConfig(const DeviceConfig &config);
    bool connectToDevice(int timeoutMs = -1);
    void disconnectFromDevice(bool clearError = true);
    bool isConnected() const;
    QString statusText() const;
    QString lastError() const;
    QString lastReply() const;
    QString peerDescription() const;

public slots:
    void applyDeviceConfigAsync(const DeviceConfig &config, bool disconnectFirst);
    void connectToDeviceAsync(const DeviceConfig &config);
    void disconnectFromDeviceAsync();
    void sendResultAsync(const QString &inspectionId, bool isOk);

signals:
    void deviceConfigApplied(const QString &statusText, bool connected);
    void connectCompleted(
        bool success,
        const QString &error,
        const QString &peerDescription,
        const QString &statusText,
        bool connected);
    void disconnectCompleted(
        const QString &peerDescription,
        const QString &statusText,
        bool connected);
    void sendCompleted(
        const QString &inspectionId,
        bool success,
        const QString &reply,
        const QString &error,
        const QString &peerDescription,
        const QString &statusText,
        bool connected);

private:
    void sendResultAsyncWithTimeout(const QString &inspectionId, bool isOk, int timeoutMs);

    TcpManager m_tcpManager;
};
