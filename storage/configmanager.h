#pragma once

#include <QString>

#include "models/deviceconfig.h"
#include "models/inputsource.h"
#include "models/visionparam.h"

// ConfigManager 负责配置持久化：
// 使用 INI 文件读写视觉参数、输入源配置、通信配置和日志级别。
class ConfigManager
{
public:
    explicit ConfigManager(QString configPath = QString());

    // 配置读取接口。
    VisionParam loadVisionParam() const;
    DeviceConfig loadDeviceConfig() const;
    InputSourceConfig loadInputSourceConfig() const;
    QString loadLogLevel() const;

    // 配置写入接口。
    void saveVisionParam(const VisionParam &param) const;
    void saveDeviceConfig(const DeviceConfig &config) const;
    void saveInputSourceConfig(const InputSourceConfig &config) const;
    void saveLogLevel(const QString &levelName) const;

    // 只读查询：返回当前生效配置文件路径。
    QString configFilePath() const;

private:
    // 解析最终配置路径：优先外部传入，其次应用目录默认路径。
    QString resolvedConfigPath() const;

    QString m_configPath;
};
