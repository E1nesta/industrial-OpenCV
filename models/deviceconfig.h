#pragma once

#include <QString>

struct DeviceConfig
{
    QString ip = "127.0.0.1";
    int port = 9000;
    QString comName = "COM1";
    int baudRate = 115200;
};

