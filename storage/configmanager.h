#pragma once

#include <QString>

#include "models/deviceconfig.h"
#include "models/visionparam.h"

class ConfigManager
{
public:
    explicit ConfigManager(QString configPath = QString());

    VisionParam loadVisionParam() const;
    DeviceConfig loadDeviceConfig() const;
    void saveVisionParam(const VisionParam &param) const;
    void saveDeviceConfig(const DeviceConfig &config) const;
    QString configFilePath() const;

private:
    QString resolvedConfigPath() const;

    QString m_configPath;
};
