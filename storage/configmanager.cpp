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

    if (config.type == InputSourceType::Camera && config.sourceName.trimmed().isEmpty()) {
        // 摄像头模式缺少名称时补默认名，便于 UI 直接展示。
        config.sourceName = QStringLiteral("camera-%1").arg(config.deviceIndex);
    }

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
    // 写配置前确保目录存在，避免首次启动写入失败。
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    QSettings settings(filePath, QSettings::IniFormat);
    settings.beginGroup("vision");
    settings.setValue("threshold", param.threshold);
    settings.setValue("minArea", param.minArea);
    settings.setValue("maxArea", param.maxArea);
    settings.setValue("enableMorphology", param.enableMorphology);
    settings.setValue("grayConversionMode", grayConversionModeToString(param.grayConversionMode));
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

void ConfigManager::saveInputSourceConfig(const InputSourceConfig &config) const
{
    const QString filePath = resolvedConfigPath();
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    QSettings settings(filePath, QSettings::IniFormat);
    settings.beginGroup("input");
    settings.setValue("type", inputSourceTypeToString(config.type));
    settings.setValue("sourcePath", config.sourcePath);
    settings.setValue("sourceName", config.sourceName);
    settings.setValue("deviceIndex", config.deviceIndex);
    settings.setValue("previewIntervalMs", config.previewIntervalMs);
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
