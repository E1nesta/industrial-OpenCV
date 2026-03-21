#include "storage/configmanager.h"

#include <utility>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include "common/constants.h"

ConfigManager::ConfigManager(QString configPath)
    : m_configPath(std::move(configPath))
{
}

VisionParam ConfigManager::loadVisionParam() const
{
    VisionParam param;
    QSettings settings(resolvedConfigPath(), QSettings::IniFormat);

    settings.beginGroup("vision");
    param.threshold = settings.value("threshold", param.threshold).toInt();
    param.minArea = settings.value("minArea", param.minArea).toInt();
    param.maxArea = settings.value("maxArea", param.maxArea).toInt();
    param.enableMorphology = settings.value("enableMorphology", param.enableMorphology).toBool();
    param.imageSavePath = settings.value("imageSavePath", param.imageSavePath).toString();

    const int roiX = settings.value("roiX", 0).toInt();
    const int roiY = settings.value("roiY", 0).toInt();
    const int roiWidth = settings.value("roiWidth", 0).toInt();
    const int roiHeight = settings.value("roiHeight", 0).toInt();
    param.roi = QRect(roiX, roiY, roiWidth, roiHeight);
    settings.endGroup();

    return param;
}

DeviceConfig ConfigManager::loadDeviceConfig() const
{
    DeviceConfig config;
    QSettings settings(resolvedConfigPath(), QSettings::IniFormat);

    settings.beginGroup("device");
    config.ip = settings.value("ip", config.ip).toString();
    config.port = settings.value("port", config.port).toInt();
    config.comName = settings.value("comName", config.comName).toString();
    config.baudRate = settings.value("baudRate", config.baudRate).toInt();
    config.tcpConnectTimeoutMs =
        settings.value("tcpConnectTimeoutMs", config.tcpConnectTimeoutMs).toInt();
    config.tcpSendTimeoutMs =
        settings.value("tcpSendTimeoutMs", config.tcpSendTimeoutMs).toInt();
    config.tcpSendRetryCount =
        settings.value("tcpSendRetryCount", config.tcpSendRetryCount).toInt();
    settings.endGroup();

    return config;
}

QString ConfigManager::loadLogLevel() const
{
    QSettings settings(resolvedConfigPath(), QSettings::IniFormat);
    settings.beginGroup("logger");
    const QString levelName = settings.value("minimumLevel", QStringLiteral("INFO")).toString();
    settings.endGroup();
    return levelName;
}

void ConfigManager::saveVisionParam(const VisionParam &param) const
{
    const QString filePath = resolvedConfigPath();
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    QSettings settings(filePath, QSettings::IniFormat);
    settings.beginGroup("vision");
    settings.setValue("threshold", param.threshold);
    settings.setValue("minArea", param.minArea);
    settings.setValue("maxArea", param.maxArea);
    settings.setValue("enableMorphology", param.enableMorphology);
    settings.setValue("imageSavePath", param.imageSavePath);
    settings.setValue("roiX", param.roi.x());
    settings.setValue("roiY", param.roi.y());
    settings.setValue("roiWidth", param.roi.width());
    settings.setValue("roiHeight", param.roi.height());
    settings.endGroup();
    settings.sync();
}

void ConfigManager::saveDeviceConfig(const DeviceConfig &config) const
{
    const QString filePath = resolvedConfigPath();
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    QSettings settings(filePath, QSettings::IniFormat);
    settings.beginGroup("device");
    settings.setValue("ip", config.ip);
    settings.setValue("port", config.port);
    settings.setValue("comName", config.comName);
    settings.setValue("baudRate", config.baudRate);
    settings.setValue("tcpConnectTimeoutMs", config.tcpConnectTimeoutMs);
    settings.setValue("tcpSendTimeoutMs", config.tcpSendTimeoutMs);
    settings.setValue("tcpSendRetryCount", config.tcpSendRetryCount);
    settings.endGroup();
    settings.sync();
}

void ConfigManager::saveLogLevel(const QString &levelName) const
{
    const QString filePath = resolvedConfigPath();
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    QSettings settings(filePath, QSettings::IniFormat);
    settings.beginGroup("logger");
    settings.setValue("minimumLevel", levelName);
    settings.endGroup();
    settings.sync();
}

QString ConfigManager::configFilePath() const
{
    return resolvedConfigPath();
}

QString ConfigManager::resolvedConfigPath() const
{
    if (!m_configPath.isEmpty()) {
        return m_configPath;
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    return appDir.filePath(
        QStringLiteral("%1/%2")
            .arg(constants::kConfigDirectoryName, constants::kConfigFileName));
}
