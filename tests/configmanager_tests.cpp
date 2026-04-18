#include <QtTest>

#include <QDir>
#include <QSettings>
#include <QTemporaryDir>

#include "infrastructure/config/configmanager.h"

class ConfigManagerTests : public QObject
{
    Q_OBJECT

private slots:
    void persistsGrayConversionMode();
    void fallsBackToStableManualForUnknownGrayMode();
    void sanitizesLoadedValues();
};

void ConfigManagerTests::persistsGrayConversionMode()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Temporary directory should be created");

    const QString configPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));
    ConfigManager configManager(configPath);

    Recipe saved;
    saved.threshold = 90;
    saved.grayConversionMode = GrayConversionMode::OpenCvCvtColor;

    configManager.saveRecipe(saved);
    const Recipe loaded = configManager.loadRecipe();

    QCOMPARE(loaded.threshold, 90);
    QCOMPARE(loaded.grayConversionMode, GrayConversionMode::OpenCvCvtColor);
}

void ConfigManagerTests::fallsBackToStableManualForUnknownGrayMode()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Temporary directory should be created");

    const QString configPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));
    QSettings settings(configPath, QSettings::IniFormat);
    settings.beginGroup("vision");
    settings.setValue("grayConversionMode", QStringLiteral("unexpected_mode"));
    settings.endGroup();
    settings.sync();

    ConfigManager configManager(configPath);
    const Recipe loaded = configManager.loadRecipe();

    QCOMPARE(loaded.grayConversionMode, GrayConversionMode::StableManual);
}

void ConfigManagerTests::sanitizesLoadedValues()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Temporary directory should be created");

    const QString configPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));
    QSettings settings(configPath, QSettings::IniFormat);

    settings.beginGroup("vision");
    settings.setValue("threshold", -30);
    settings.setValue("minArea", -10);
    settings.setValue("maxArea", 5);
    settings.setValue("imageSavePath", QStringLiteral("   "));
    settings.endGroup();

    settings.beginGroup("device");
    settings.setValue("ip", QStringLiteral(" 127.0.0.1 "));
    settings.setValue("port", 70000);
    settings.setValue("tcpConnectTimeoutMs", 5);
    settings.setValue("tcpSendTimeoutMs", -20);
    settings.setValue("tcpSendRetryCount", -1);
    settings.endGroup();

    settings.beginGroup("input");
    settings.setValue("type", QStringLiteral("camera"));
    settings.setValue("deviceIndex", -2);
    settings.setValue("previewIntervalMs", 0);
    settings.setValue("sourceName", QStringLiteral("   "));
    settings.endGroup();
    settings.sync();

    ConfigManager configManager(configPath);
    const Recipe recipe = configManager.loadRecipe();
    const DeviceConfig device = configManager.loadDeviceConfig();
    const InputSourceConfig input = configManager.loadInputSourceConfig();

    QCOMPARE(recipe.threshold, 0);
    QCOMPARE(recipe.minArea, 0);
    QCOMPARE(recipe.maxArea, 5);
    QCOMPARE(recipe.imageSavePath, QStringLiteral("data/images"));

    QCOMPARE(device.ip, QStringLiteral("127.0.0.1"));
    QCOMPARE(device.port, 0);
    QCOMPARE(device.tcpConnectTimeoutMs, 100);
    QCOMPARE(device.tcpSendTimeoutMs, 100);
    QCOMPARE(device.tcpSendRetryCount, 0);

    QCOMPARE(input.deviceIndex, 0);
    QCOMPARE(input.previewIntervalMs, 1);
    QCOMPARE(input.sourceName, QStringLiteral("camera-0"));
}

QTEST_GUILESS_MAIN(ConfigManagerTests)

#include "configmanager_tests.moc"
