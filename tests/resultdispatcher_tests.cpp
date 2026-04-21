#include <QtTest>

#include <QDateTime>
#include <QTemporaryDir>

#include <opencv2/core/mat.hpp>

#include "application/orchestrators/resultdispatcher.h"
#include "common/logging/logmanager.h"

namespace
{
InspectionDispatchContext makeDispatchContext(bool shouldSendTcpResult)
{
    InspectionDispatchContext context;
    context.execution.request.inspectionId = QStringLiteral("inspection-001");
    context.shouldSendTcpResult = shouldSendTcpResult;
    context.tcpDeviceConfig.ip = QStringLiteral("192.168.1.88");
    context.tcpDeviceConfig.port = 12345;
    context.execution.request.frame.meta.captureId = QStringLiteral("capture-001");
    context.execution.request.frame.meta.sourceType = InputSourceType::VideoFile;
    context.execution.request.frame.meta.sourceName = QStringLiteral("demo.mp4");
    context.execution.request.frame.meta.frameIndex = 7;
    context.execution.request.frame.meta.capturedAt = QDateTime::currentDateTime();
    context.execution.request.recipe.threshold = 128;

    context.execution.result.inspectionId = context.execution.request.inspectionId;
    context.execution.result.isOk = false;
    context.execution.result.defectCount = 2;
    context.execution.result.elapsedMs = 12.5;
    context.execution.result.summaryText = QStringLiteral("AOI 外观检测 NG：检测到 2 处缺陷。");

    context.execution.annotatedImage = cv::Mat(24, 32, CV_8UC3, cv::Scalar(10, 20, 30));
    return context;
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
    const InspectionDispatchContext context = makeDispatchContext(false);
    int persistenceCallCount = 0;
    InspectionExecutionPayload persistedExecutionPayload;

    const ResultDispatchOutcome outcome = dispatcher.dispatch(
        context,
        &logManager,
        [&persistenceCallCount, &persistedExecutionPayload](const InspectionExecutionPayload &value) {
            ++persistenceCallCount;
            persistedExecutionPayload = value;
        },
        {});

    QCOMPARE(persistenceCallCount, 1);
    QCOMPARE(persistedExecutionPayload.result.inspectionId, QStringLiteral("inspection-001"));
    QCOMPARE(outcome.result.inspectionId, context.execution.result.inspectionId);
    QCOMPARE(outcome.result.defectCount, context.execution.result.defectCount);
    QCOMPARE(outcome.statusMessage, QStringLiteral("AOI 外观检测 NG：检测到 2 处缺陷。"));
    QVERIFY(!outcome.resultImage.isNull());
    QCOMPARE(outcome.resultImage.width(), context.execution.annotatedImage.cols);
    QCOMPARE(outcome.resultImage.height(), context.execution.annotatedImage.rows);
}

void ResultDispatcherTests::dispatchInvokesTcpOnlyWhenEnabled()
{
    ResultDispatcher dispatcher;
    const InspectionDispatchContext enabledContext = makeDispatchContext(true);
    const InspectionDispatchContext disabledContext = makeDispatchContext(false);
    int tcpCallCount = 0;
    QString lastInspectionId;
    bool lastIsOk = true;
    DeviceConfig lastConfig;

    dispatcher.dispatch(
        enabledContext,
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
    QCOMPARE(lastIsOk, enabledContext.execution.result.isOk);
    QCOMPARE(lastConfig.ip, QStringLiteral("192.168.1.88"));
    QCOMPARE(lastConfig.port, 12345);

    dispatcher.dispatch(
        disabledContext,
        nullptr,
        {},
        [&tcpCallCount](const QString &, bool, const DeviceConfig &) { ++tcpCallCount; });

    QCOMPARE(tcpCallCount, 1);
}

void ResultDispatcherTests::dispatchHandlesNullLoggerAndEmptyCallbacks()
{
    ResultDispatcher dispatcher;
    const InspectionDispatchContext context = makeDispatchContext(false);

    const ResultDispatchOutcome outcome = dispatcher.dispatch(context, nullptr, {}, {});

    QCOMPARE(outcome.result.inspectionId, QStringLiteral("inspection-001"));
    QCOMPARE(outcome.statusMessage, QStringLiteral("AOI 外观检测 NG：检测到 2 处缺陷。"));
    QVERIFY(!outcome.resultImage.isNull());
}

QTEST_GUILESS_MAIN(ResultDispatcherTests)

#include "resultdispatcher_tests.moc"
