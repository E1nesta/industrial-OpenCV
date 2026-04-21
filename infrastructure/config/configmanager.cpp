// 基础设施配置：configmanager.cpp 负责配置加载、保存与回填。
// 本文件连接配置存储与运行态参数，保证配置语义一致。
#include "infrastructure/config/configmanager.h"

#include <algorithm>
#include <utility>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include "common/config/constants.h"

ConfigManager::ConfigManager(QString configPath)
    : m_configPath(std::move(configPath))
{
}

Recipe ConfigManager::normalizeRecipe(const Recipe &recipe)
{
    Recipe normalized = recipe;
    normalized.recipeName = normalized.recipeName.trimmed();
    if (normalized.recipeName.isEmpty()) {
        normalized.recipeName = QStringLiteral("default-aoi");
    }
    if (!normalized.saveSourceImage && !normalized.saveResultImage) {
        // 配方持久化阶段至少保留一种留痕图像，避免生成无法回看的记录。
        normalized.saveResultImage = true;
    }
    normalized.threshold = std::clamp(normalized.threshold, 0, 255);
    normalized.minArea = std::max(0, normalized.minArea);
    if (normalized.maxArea > 0 && normalized.maxArea < normalized.minArea) {
        normalized.maxArea = normalized.minArea;
    }
    normalized.roi.setWidth(std::max(0, normalized.roi.width()));
    normalized.roi.setHeight(std::max(0, normalized.roi.height()));
    normalized.imageSavePath = normalized.imageSavePath.trimmed();
    if (normalized.imageSavePath.isEmpty()) {
        normalized.imageSavePath = QStringLiteral("data/images");
    }
    return normalized;
}

DeviceConfig ConfigManager::normalizeDeviceConfig(const DeviceConfig &config)
{
    DeviceConfig normalized = config;
    normalized.ip = normalized.ip.trimmed();
    normalized.port = (normalized.port > 0 && normalized.port <= 65535) ? normalized.port : 0;
    normalized.comName = normalized.comName.trimmed();
    normalized.baudRate = std::max(0, normalized.baudRate);
    normalized.tcpConnectTimeoutMs = std::max(100, normalized.tcpConnectTimeoutMs);
    normalized.tcpSendTimeoutMs = std::max(100, normalized.tcpSendTimeoutMs);
    normalized.tcpSendRetryCount = std::max(0, normalized.tcpSendRetryCount);
    return normalized;
}

InputSourceConfig ConfigManager::normalizeInputSourceConfig(const InputSourceConfig &config)
{
    InputSourceConfig normalized = config;
    normalized.sourcePath = normalized.sourcePath.trimmed();
    normalized.sourceName = normalized.sourceName.trimmed();
    normalized.deviceIndex = std::max(0, normalized.deviceIndex);
    normalized.previewIntervalMs = std::max(1, normalized.previewIntervalMs);
    if (normalized.type == InputSourceType::Camera && normalized.sourceName.isEmpty()) {
        normalized.sourceName = QStringLiteral("camera-%1").arg(normalized.deviceIndex);
    }
    return normalized;
}

Recipe ConfigManager::loadRecipe() const
{
    Recipe param;
    QSettings settings(resolvedConfigPath(), QSettings::IniFormat);

    // 视觉参数统一从 vision 分组读取，缺失字段回退到结构默认值。
    settings.beginGroup("vision");
    param.recipeName = settings.value("recipeName", param.recipeName).toString();
    param.enableDefectDetection =
        settings.value("enableDefectDetection", param.enableDefectDetection).toBool();
    param.threshold = settings.value("threshold", param.threshold).toInt();
    param.minArea = settings.value("minArea", param.minArea).toInt();
    param.maxArea = settings.value("maxArea", param.maxArea).toInt();
    param.enableMorphology = settings.value("enableMorphology", param.enableMorphology).toBool();
    param.saveSourceImage = settings.value("saveSourceImage", param.saveSourceImage).toBool();
    param.saveResultImage = settings.value("saveResultImage", param.saveResultImage).toBool();
    param.enableTcpResult = settings.value("enableTcpResult", param.enableTcpResult).toBool();
    param.grayConversionMode = grayConversionModeFromString(
        settings.value("grayConversionMode", grayConversionModeToString(param.grayConversionMode)).toString());
    param.imageSavePath = settings.value("imageSavePath", param.imageSavePath).toString();

    const int roiX = settings.value("roiX", 0).toInt();
    const int roiY = settings.value("roiY", 0).toInt();
    const int roiWidth = settings.value("roiWidth", 0).toInt();
    const int roiHeight = settings.value("roiHeight", 0).toInt();
    param.roi = QRect(roiX, roiY, roiWidth, roiHeight);
    settings.endGroup();

    return normalizeRecipe(param);
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

    return normalizeDeviceConfig(config);
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

    return normalizeInputSourceConfig(config);
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

    const Recipe sanitized = normalizeRecipe(param);
    QSettings settings(filePath, QSettings::IniFormat);
    settings.beginGroup("vision");
    settings.setValue("recipeName", sanitized.recipeName);
    settings.setValue("enableDefectDetection", sanitized.enableDefectDetection);
    settings.setValue("threshold", sanitized.threshold);
    settings.setValue("minArea", sanitized.minArea);
    settings.setValue("maxArea", sanitized.maxArea);
    settings.setValue("enableMorphology", sanitized.enableMorphology);
    settings.setValue("saveSourceImage", sanitized.saveSourceImage);
    settings.setValue("saveResultImage", sanitized.saveResultImage);
    settings.setValue("enableTcpResult", sanitized.enableTcpResult);
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

    const DeviceConfig sanitized = normalizeDeviceConfig(config);
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

    const InputSourceConfig sanitized = normalizeInputSourceConfig(config);
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
