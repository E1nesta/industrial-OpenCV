#pragma once

#include <QMetaType>
#include <QString>

struct DeviceConfig
{
    QString ip = "127.0.0.1";
    int port = 9000;
    QString comName = "COM1";
    int baudRate = 115200;
    int tcpConnectTimeoutMs = 2000;
    int tcpSendTimeoutMs = 2000;
    int tcpSendRetryCount = 1;
};

Q_DECLARE_METATYPE(DeviceConfig)
