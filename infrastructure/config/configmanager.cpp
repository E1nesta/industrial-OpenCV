#include "infrastructure/config/configmanager.h"

#include <algorithm>
#include <utility>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include "common/config/constants.h"

namespace
{
Recipe sanitizeRecipe(const Recipe &recipe)
{
    Recipe sanitized = recipe;
    sanitized.threshold = std::clamp(sanitized.threshold, 0, 255);
    sanitized.minArea = std::max(0, sanitized.minArea);
    if (sanitized.maxArea > 0 && sanitized.maxArea < sanitized.minArea) {
        sanitized.maxArea = sanitized.minArea;
    }
    sanitized.roi.setWidth(std::max(0, sanitized.roi.width()));
    sanitized.roi.setHeight(std::max(0, sanitized.roi.height()));
    sanitized.imageSavePath = sanitized.imageSavePath.trimmed();
    if (sanitized.imageSavePath.isEmpty()) {
        sanitized.imageSavePath = QStringLiteral("data/images");
    }
    return sanitized;
}

DeviceConfig sanitizeDeviceConfig(const DeviceConfig &config)
{
    DeviceConfig sanitized = config;
    sanitized.ip = sanitized.ip.trimmed();
    sanitized.port = (sanitized.port > 0 && sanitized.port <= 65535) ? sanitized.port : 0;
    sanitized.comName = sanitized.comName.trimmed();
    sanitized.baudRate = std::max(0, sanitized.baudRate);
    sanitized.tcpConnectTimeoutMs = std::max(100, sanitized.tcpConnectTimeoutMs);
    sanitized.tcpSendTimeoutMs = std::max(100, sanitized.tcpSendTimeoutMs);
    sanitized.tcpSendRetryCount = std::max(0, sanitized.tcpSendRetryCount);
    return sanitized;
}

InputSourceConfig sanitizeInputSourceConfig(const InputSourceConfig &config)
{
    InputSourceConfig sanitized = config;
    sanitized.sourcePath = sanitized.sourcePath.trimmed();
    sanitized.sourceName = sanitized.sourceName.trimmed();
    sanitized.deviceIndex = std::max(0, sanitized.deviceIndex);
    sanitized.previewIntervalMs = std::max(1, sanitized.previewIntervalMs);
    if (sanitized.type == InputSourceType::Camera && sanitized.sourceName.isEmpty()) {
        sanitized.sourceName = QStringLiteral("camera-%1").arg(sanitized.deviceIndex);
    }
    return sanitized;
}
} // namespace

ConfigManager::ConfigManager(QString configPath)
    : m_configPath(std::move(configPath))
{
}

Recipe ConfigManager::loadRecipe() const
{
    Recipe param;
    QSettings settings(resolvedConfigPath(), QSettings::IniFormat);

    // 视觉参数统一从 vision 分组读取，缺失字段回退到结构默认值。
    settings.beginGroup("vision");
    param.threshold = settings.value("threshold", param.threshold).toInt();
    param.minArea = settings.value("minArea", param.minArea).toInt();
    param.maxArea = settings.value("maxArea", param.maxArea).toInt();
    param.enableMorphology = settings.value("enableMorphology", param.enableMorphology).toBool();
    param.grayConversionMode = grayConversionModeFromString(
        settings.value("grayConversionMode", grayConversionModeToString(param.grayConversionMode)).toString());
    param.imageSavePath = settings.value("imageSavePath", param.imageSavePath).toString();

    const int roiX = settings.value("roiX", 0).toInt();
    const int roiY = settings.value("roiY", 0).toInt();
    const int roiWidth = settings.value("roiWidth", 0).toInt();
    const int roiHeight = settings.value("roiHeight", 0).toInt();
    param.roi = QRect(roiX, roiY, roiWidth, roiHeight);
    settings.endGroup();

    return sanitizeRecipe(param);
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

    return sanitizeDeviceConfig(config);
}

InputSourceConfig ConfigManager::loadInputSourceConfig() const
{
    InputSourceConfig config;
    QSettings settings(resolvedConfigPath(), QSettings::IniFormat);

    // 输入源配置按“类型 + 路径/设备号 + 预览节拍”组织。
    settings.beginGroup("input");
    config.type = inputSourceTypeFromString(
        settings.value("type", inputSourceTypeToString(config.type)).toString());
    config.sourcePath = settings.value("sourcePath", config.sourcePath).toString();
    config.sourceName = settings.value("sourceName", config.sourceName).toString();
    config.deviceIndex = settings.value("deviceIndex", config.deviceIndex).toInt();
    config.previewIntervalMs =
        settings.value("previewIntervalMs", config.previewIntervalMs).toInt();
    settings.endGroup();

    return sanitizeInputSourceConfig(config);
}

QString ConfigManager::loadLogLevel() const
{
    QSettings settings(resolvedConfigPath(), QSettings::IniFormat);
    settings.beginGroup("logger");
    const QString levelName = settings.value("minimumLevel", QStringLiteral("INFO")).toString();
    settings.endGroup();
    return levelName;
}

void ConfigManager::saveRecipe(const Recipe &param) const
{
    const QString filePath = resolvedConfigPath();
    // 写配置前确保目录存在，避免首次启动写入失败。
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    const Recipe sanitized = sanitizeRecipe(param);
    QSettings settings(filePath, QSettings::IniFormat);
    settings.beginGroup("vision");
    settings.setValue("threshold", sanitized.threshold);
    settings.setValue("minArea", sanitized.minArea);
    settings.setValue("maxArea", sanitized.maxArea);
    settings.setValue("enableMorphology", sanitized.enableMorphology);
    settings.setValue("grayConversionMode", grayConversionModeToString(sanitized.grayConversionMode));
    settings.setValue("imageSavePath", sanitized.imageSavePath);
    settings.setValue("roiX", sanitized.roi.x());
    settings.setValue("roiY", sanitized.roi.y());
    settings.setValue("roiWidth", sanitized.roi.width());
    settings.setValue("roiHeight", sanitized.roi.height());
    settings.endGroup();
    settings.sync();
}

void ConfigManager::saveDeviceConfig(const DeviceConfig &config) const
{
    const QString filePath = resolvedConfigPath();
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    const DeviceConfig sanitized = sanitizeDeviceConfig(config);
    QSettings settings(filePath, QSettings::IniFormat);
    settings.beginGroup("device");
    settings.setValue("ip", sanitized.ip);
    settings.setValue("port", sanitized.port);
    settings.setValue("comName", sanitized.comName);
    settings.setValue("baudRate", sanitized.baudRate);
    settings.setValue("tcpConnectTimeoutMs", sanitized.tcpConnectTimeoutMs);
    settings.setValue("tcpSendTimeoutMs", sanitized.tcpSendTimeoutMs);
    settings.setValue("tcpSendRetryCount", sanitized.tcpSendRetryCount);
    settings.endGroup();
    settings.sync();
}

void ConfigManager::saveInputSourceConfig(const InputSourceConfig &config) const
{
    const QString filePath = resolvedConfigPath();
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    const InputSourceConfig sanitized = sanitizeInputSourceConfig(config);
    QSettings settings(filePath, QSettings::IniFormat);
    settings.beginGroup("input");
    settings.setValue("type", inputSourceTypeToString(sanitized.type));
    settings.setValue("sourcePath", sanitized.sourcePath);
    settings.setValue("sourceName", sanitized.sourceName);
    settings.setValue("deviceIndex", sanitized.deviceIndex);
    settings.setValue("previewIntervalMs", sanitized.previewIntervalMs);
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
    // 外部传入路径优先，便于测试或多配置场景复用。
    if (!m_configPath.isEmpty()) {
        return m_configPath;
    }

    // 默认落盘到应用目录下 config 子目录。
    const QDir appDir(QCoreApplication::applicationDirPath());
    return appDir.filePath(
        QStringLiteral("%1/%2")
            .arg(constants::kConfigDirectoryName, constants::kConfigFileName));
}
