// 领域实体：DeviceConfig 定义设备通信配置。
// 该对象用于 TCP/串口等外部通信参数传递。
#pragma once

#include <QMetaType>
#include <QString>

struct DeviceConfig
{
    // 目标设备 IP。
    QString ip = "127.0.0.1";
    // 目标设备端口。
    int port = 9000;
    // 串口名称。
    QString comName = "COM1";
    // 串口波特率。
    int baudRate = 115200;
    // TCP 连接超时，单位毫秒。
    int tcpConnectTimeoutMs = 2000;
    // TCP 发送超时，单位毫秒。
    int tcpSendTimeoutMs = 2000;
    // TCP 发送失败重试次数。
    int tcpSendRetryCount = 1;
};

Q_DECLARE_METATYPE(DeviceConfig)
