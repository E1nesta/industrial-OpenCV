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
    void keepsAtLeastOneArchiveImageEnabled();
    void sanitizesLoadedValues();
    void normalizesRuntimeValuesThroughSharedEntryPoints();
};

void ConfigManagerTests::persistsGrayConversionMode()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Temporary directory should be created");

    const QString configPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));
    ConfigManager configManager(configPath);

    Recipe saved;
    saved.recipeName = QStringLiteral("camera-aoi");
    saved.enableDefectDetection = false;
    saved.saveSourceImage = false;
    saved.saveResultImage = true;
    saved.enableTcpResult = false;
    saved.threshold = 90;
    saved.grayConversionMode = GrayConversionMode::OpenCvCvtColor;

    configManager.saveRecipe(saved);
    const Recipe loaded = configManager.loadRecipe();

    QCOMPARE(loaded.recipeName, QStringLiteral("camera-aoi"));
    QCOMPARE(loaded.enableDefectDetection, false);
    QCOMPARE(loaded.saveSourceImage, false);
    QCOMPARE(loaded.saveResultImage, true);
    QCOMPARE(loaded.enableTcpResult, false);
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

void ConfigManagerTests::keepsAtLeastOneArchiveImageEnabled()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Temporary directory should be created");

    const QString configPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));
    ConfigManager configManager(configPath);

    Recipe saved;
    saved.saveSourceImage = false;
    saved.saveResultImage = false;

    configManager.saveRecipe(saved);
    const Recipe loaded = configManager.loadRecipe();

    QVERIFY(!loaded.saveSourceImage || loaded.saveResultImage);
    QVERIFY(loaded.saveResultImage);
}

void ConfigManagerTests::sanitizesLoadedValues()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Temporary directory should be created");

    const QString configPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));
    QSettings settings(configPath, QSettings::IniFormat);

    settings.beginGroup("vision");
    settings.setValue("recipeName", QStringLiteral("   "));
    settings.setValue("threshold", -30);
    settings.setValue("minArea", -10);
    settings.setValue("maxArea", 5);
    settings.setValue("imageSavePath", QStringLiteral("   "));
    settings.setValue("saveSourceImage", false);
    settings.setValue("saveResultImage", false);
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

    QCOMPARE(recipe.recipeName, QStringLiteral("default-aoi"));
    QCOMPARE(recipe.threshold, 0);
    QCOMPARE(recipe.minArea, 0);
    QCOMPARE(recipe.maxArea, 5);
    QCOMPARE(recipe.imageSavePath, QStringLiteral("data/images"));
    QVERIFY(!recipe.saveSourceImage || recipe.saveResultImage);
    QVERIFY(recipe.saveResultImage);

    QCOMPARE(device.ip, QStringLiteral("127.0.0.1"));
    QCOMPARE(device.port, 0);
    QCOMPARE(device.tcpConnectTimeoutMs, 100);
    QCOMPARE(device.tcpSendTimeoutMs, 100);
    QCOMPARE(device.tcpSendRetryCount, 0);

    QCOMPARE(input.deviceIndex, 0);
    QCOMPARE(input.previewIntervalMs, 1);
    QCOMPARE(input.sourceName, QStringLiteral("camera-0"));
}

void ConfigManagerTests::normalizesRuntimeValuesThroughSharedEntryPoints()
{
    Recipe rawRecipe;
    rawRecipe.recipeName = QStringLiteral("   ");
    rawRecipe.threshold = 400;
    rawRecipe.minArea = -3;
    rawRecipe.maxArea = 1;
    rawRecipe.saveSourceImage = false;
    rawRecipe.saveResultImage = false;
    rawRecipe.imageSavePath = QStringLiteral("   ");

    DeviceConfig rawDevice;
    rawDevice.ip = QStringLiteral(" 192.168.0.10 ");
    rawDevice.port = 70000;
    rawDevice.tcpConnectTimeoutMs = 50;
    rawDevice.tcpSendTimeoutMs = 10;
    rawDevice.tcpSendRetryCount = -2;
    rawDevice.baudRate = -115200;

    InputSourceConfig rawInput;
    rawInput.type = InputSourceType::Camera;
    rawInput.sourceName = QStringLiteral("   ");
    rawInput.deviceIndex = -5;
    rawInput.previewIntervalMs = 0;

    const Recipe recipe = ConfigManager::normalizeRecipe(rawRecipe);
    const DeviceConfig device = ConfigManager::normalizeDeviceConfig(rawDevice);
    const InputSourceConfig input = ConfigManager::normalizeInputSourceConfig(rawInput);

    QCOMPARE(recipe.recipeName, QStringLiteral("default-aoi"));
    QCOMPARE(recipe.threshold, 255);
    QCOMPARE(recipe.minArea, 0);
    QCOMPARE(recipe.maxArea, 1);
    QCOMPARE(recipe.imageSavePath, QStringLiteral("data/images"));
    QVERIFY(recipe.saveResultImage);

    QCOMPARE(device.ip, QStringLiteral("192.168.0.10"));
    QCOMPARE(device.port, 0);
    QCOMPARE(device.tcpConnectTimeoutMs, 100);
    QCOMPARE(device.tcpSendTimeoutMs, 100);
    QCOMPARE(device.tcpSendRetryCount, 0);
    QCOMPARE(device.baudRate, 0);

    QCOMPARE(input.deviceIndex, 0);
    QCOMPARE(input.previewIntervalMs, 1);
    QCOMPARE(input.sourceName, QStringLiteral("camera-0"));
}

QTEST_GUILESS_MAIN(ConfigManagerTests)

#include "configmanager_tests.moc"
