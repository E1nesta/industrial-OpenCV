#include <QtTest>

#include <QDir>
#include <QTemporaryDir>

#include "storage/configmanager.h"

class ConfigManagerTests : public QObject
{
    Q_OBJECT

private slots:
    void persistsGrayConversionMode();
    void fallsBackToStableManualForUnknownGrayMode();
};

void ConfigManagerTests::persistsGrayConversionMode()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Temporary directory should be created");

    const QString configPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));
    ConfigManager configManager(configPath);

    VisionParam saved;
    saved.threshold = 90;
    saved.grayConversionMode = GrayConversionMode::OpenCvCvtColor;

    configManager.saveVisionParam(saved);
    const VisionParam loaded = configManager.loadVisionParam();

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
    const VisionParam loaded = configManager.loadVisionParam();

    QCOMPARE(loaded.grayConversionMode, GrayConversionMode::StableManual);
}

QTEST_GUILESS_MAIN(ConfigManagerTests)

#include "configmanager_tests.moc"
