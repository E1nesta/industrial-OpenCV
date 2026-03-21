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
    QString loadLogLevel() const;
    void saveVisionParam(const VisionParam &param) const;
    void saveDeviceConfig(const DeviceConfig &config) const;
    void saveLogLevel(const QString &levelName) const;
    QString configFilePath() const;

private:
    QString resolvedConfigPath() const;

    QString m_configPath;
};
