#include <QtTest>

#include <QDateTime>
#include <QTemporaryDir>

#include <opencv2/core/mat.hpp>

#include "application/orchestrators/resultdispatcher.h"
#include "common/logging/logmanager.h"

namespace
{
InspectionOutput makeOutput(bool shouldSendTcpResult)
{
    InspectionOutput output;
    output.request.inspectionId = QStringLiteral("inspection-001");
    output.request.shouldSendTcpResult = shouldSendTcpResult;
    output.request.tcpDeviceConfig.ip = QStringLiteral("192.168.1.88");
    output.request.tcpDeviceConfig.port = 12345;
    output.request.frame.meta.captureId = QStringLiteral("capture-001");
    output.request.frame.meta.sourceType = InputSourceType::VideoFile;
    output.request.frame.meta.sourceName = QStringLiteral("demo.mp4");
    output.request.frame.meta.frameIndex = 7;
    output.request.frame.meta.capturedAt = QDateTime::currentDateTime();
    output.request.recipe.threshold = 128;

    output.result.inspectionId = output.request.inspectionId;
    output.result.isOk = false;
    output.result.defectCount = 2;
    output.result.processTimeMs = 12.5;
    output.result.message = QStringLiteral("检测到 2 处缺陷");

    output.annotatedImage = cv::Mat(24, 32, CV_8UC3, cv::Scalar(10, 20, 30));
    return output;
}
} // namespace

class ResultDispatcherTests : public QObject
{
    Q_OBJECT

private slots:
    void dispatchReturnsOutcomeAndInvokesPersistence();
    void dispatchInvokesTcpOnlyWhenEnabled();
    void dispatchHandlesNullLoggerAndEmptyCallbacks();
};

void ResultDispatcherTests::dispatchReturnsOutcomeAndInvokesPersistence()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    ResultDispatcher dispatcher;
    LogManager logManager(nullptr, tempDir.filePath(QStringLiteral("logs")));
    const InspectionOutput output = makeOutput(false);
    int persistenceCallCount = 0;
    InspectionOutput persistedOutput;

    const ResultDispatchOutcome outcome = dispatcher.dispatch(
        output,
        &logManager,
        [&persistenceCallCount, &persistedOutput](const InspectionOutput &value) {
            ++persistenceCallCount;
            persistedOutput = value;
        },
        {});

    QCOMPARE(persistenceCallCount, 1);
    QCOMPARE(persistedOutput.result.inspectionId, QStringLiteral("inspection-001"));
    QCOMPARE(outcome.result.inspectionId, output.result.inspectionId);
    QCOMPARE(outcome.result.defectCount, output.result.defectCount);
    QCOMPARE(outcome.statusMessage, QStringLiteral("检测完成：NG，缺陷 2 处，耗时 12.50 ms"));
    QVERIFY(!outcome.resultImage.isNull());
    QCOMPARE(outcome.resultImage.width(), output.annotatedImage.cols);
    QCOMPARE(outcome.resultImage.height(), output.annotatedImage.rows);
}

void ResultDispatcherTests::dispatchInvokesTcpOnlyWhenEnabled()
{
    ResultDispatcher dispatcher;
    const InspectionOutput enabledOutput = makeOutput(true);
    const InspectionOutput disabledOutput = makeOutput(false);
    int tcpCallCount = 0;
    QString lastInspectionId;
    bool lastIsOk = true;
    DeviceConfig lastConfig;

    dispatcher.dispatch(
        enabledOutput,
        nullptr,
        {},
        [&tcpCallCount, &lastInspectionId, &lastIsOk, &lastConfig](
            const QString &inspectionId, bool isOk, const DeviceConfig &config) {
            ++tcpCallCount;
            lastInspectionId = inspectionId;
            lastIsOk = isOk;
            lastConfig = config;
        });

    QCOMPARE(tcpCallCount, 1);
    QCOMPARE(lastInspectionId, QStringLiteral("inspection-001"));
    QCOMPARE(lastIsOk, enabledOutput.result.isOk);
    QCOMPARE(lastConfig.ip, QStringLiteral("192.168.1.88"));
    QCOMPARE(lastConfig.port, 12345);

    dispatcher.dispatch(
        disabledOutput,
        nullptr,
        {},
        [&tcpCallCount](const QString &, bool, const DeviceConfig &) { ++tcpCallCount; });

    QCOMPARE(tcpCallCount, 1);
}

void ResultDispatcherTests::dispatchHandlesNullLoggerAndEmptyCallbacks()
{
    ResultDispatcher dispatcher;
    const InspectionOutput output = makeOutput(false);

    const ResultDispatchOutcome outcome = dispatcher.dispatch(output, nullptr, {}, {});

    QCOMPARE(outcome.result.inspectionId, QStringLiteral("inspection-001"));
    QCOMPARE(outcome.statusMessage, QStringLiteral("检测完成：NG，缺陷 2 处，耗时 12.50 ms"));
    QVERIFY(!outcome.resultImage.isNull());
}

QTEST_GUILESS_MAIN(ResultDispatcherTests)

#include "resultdispatcher_tests.moc"
