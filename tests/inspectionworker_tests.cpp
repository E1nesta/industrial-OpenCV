#include <QtTest>

#include <QDateTime>

#include <opencv2/imgproc.hpp>

#include "common/logging/logmanager.h"
#include "infrastructure/vision/inspectionworker.h"

namespace
{
constexpr const char *kGrayModeEnvName = "VISION_ALLOW_UNSTABLE_OPENCV_GRAY";

struct ScopedEnvOverride
{
    explicit ScopedEnvOverride(const QByteArray &nextValue, bool enable)
        : hadOriginal(qEnvironmentVariableIsSet(kGrayModeEnvName))
        , originalValue(qgetenv(kGrayModeEnvName))
    {
        if (enable) {
            qputenv(kGrayModeEnvName, nextValue);
        } else {
            qunsetenv(kGrayModeEnvName);
        }
    }

    ~ScopedEnvOverride()
    {
        if (hadOriginal) {
            qputenv(kGrayModeEnvName, originalValue);
        } else {
            qunsetenv(kGrayModeEnvName);
        }
    }

    bool hadOriginal = false;
    QByteArray originalValue;
};

InspectionTask makeRequest(const cv::Mat &image)
{
    InspectionTask request;
    request.inspectionId = QStringLiteral("inspection-test-001");
    request.frame.meta.captureId = QStringLiteral("capture-test-001");
    request.frame.meta.sourceType = InputSourceType::VideoFile;
    request.frame.meta.sourceName = QStringLiteral("sample.mp4");
    request.frame.meta.frameIndex = 12;
    request.frame.meta.capturedAt = QDateTime::currentDateTime();
    request.frame.image = image;
    request.recipe.threshold = 128;
    request.recipe.minArea = 50;
    request.recipe.maxArea = 500000;
    return request;
}
} // namespace

class InspectionWorkerTests : public QObject
{
    Q_OBJECT

private slots:
    void emitsCompletedForValidFrame();
    void emitsFailedForInvalidFrame();
    void emitsCanceledWhenCancellationWasRequested();
    void emitsWarningWhenGrayModeIsDowngraded();
};

void InspectionWorkerTests::emitsCompletedForValidFrame()
{
    qRegisterMetaType<InspectionExecutionPayload>("InspectionExecutionPayload");

    LogManager logManager(nullptr, QStringLiteral("tests/logs"));
    InspectionWorker worker(&logManager);

    QSignalSpy completedSpy(&worker, &InspectionWorker::completed);
    QSignalSpy failedSpy(&worker, &InspectionWorker::failed);

    cv::Mat image(1080, 1920, CV_8UC3, cv::Scalar(255, 255, 255));
    cv::rectangle(image, cv::Rect(240, 320, 140, 180), cv::Scalar(0, 0, 0), cv::FILLED);

    worker.process(makeRequest(image));

    QCOMPARE(completedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);

    const QList<QVariant> arguments = completedSpy.takeFirst();
    QVERIFY(!arguments.isEmpty());
    const InspectionExecutionPayload executionPayload =
        qvariant_cast<InspectionExecutionPayload>(arguments.at(0));
    QCOMPARE(executionPayload.result.inspectionId, QStringLiteral("inspection-test-001"));
    QCOMPARE(executionPayload.request.frame.meta.captureId, QStringLiteral("capture-test-001"));
    QVERIFY(!executionPayload.annotatedImage.empty());
}

void InspectionWorkerTests::emitsFailedForInvalidFrame()
{
    LogManager logManager(nullptr, QStringLiteral("tests/logs"));
    InspectionWorker worker(&logManager);

    QSignalSpy completedSpy(&worker, &InspectionWorker::completed);
    QSignalSpy failedSpy(&worker, &InspectionWorker::failed);

    InspectionTask request = makeRequest(cv::Mat{});
    request.frame.image = cv::Mat{};

    worker.process(request);

    QCOMPARE(completedSpy.count(), 0);
    QCOMPARE(failedSpy.count(), 1);

    const QList<QVariant> arguments = failedSpy.takeFirst();
    QCOMPARE(arguments.at(0).toString(), QStringLiteral("inspection-test-001"));
    QVERIFY(arguments.at(1).toString().contains(QStringLiteral("未提供有效的待检测帧")));
}

void InspectionWorkerTests::emitsCanceledWhenCancellationWasRequested()
{
    LogManager logManager(nullptr, QStringLiteral("tests/logs"));
    InspectionWorker worker(&logManager);

    QSignalSpy completedSpy(&worker, &InspectionWorker::completed);
    QSignalSpy failedSpy(&worker, &InspectionWorker::failed);
    QSignalSpy canceledSpy(&worker, &InspectionWorker::canceled);

    cv::Mat image(1080, 1920, CV_8UC3, cv::Scalar(255, 255, 255));
    worker.requestCancel();
    worker.process(makeRequest(image));

    QCOMPARE(completedSpy.count(), 0);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(canceledSpy.count(), 1);
    QCOMPARE(canceledSpy.takeFirst().at(0).toString(), QStringLiteral("inspection-test-001"));
}

void InspectionWorkerTests::emitsWarningWhenGrayModeIsDowngraded()
{
    ScopedEnvOverride scopedEnv({}, false);

    LogManager logManager(nullptr, QStringLiteral("tests/logs"));
    InspectionWorker worker(&logManager);

    QSignalSpy logSpy(&logManager, &LogManager::uiLogGenerated);

    InspectionTask request = makeRequest(
        cv::Mat(1080, 1920, CV_8UC3, cv::Scalar(255, 255, 255)));
    request.recipe.grayConversionMode = GrayConversionMode::OpenCvCvtColor;

    worker.process(request);

    QTRY_VERIFY(!logSpy.isEmpty());

    bool foundDowngradeWarning = false;
    for (int index = 0; index < logSpy.count(); ++index) {
        const QList<QVariant> arguments = logSpy.at(index);
        if (arguments.isEmpty()) {
            continue;
        }

        const LogEvent event = qvariant_cast<LogEvent>(arguments.constFirst());
        if (event.level == QStringLiteral("WARN")
            && event.message.contains(QStringLiteral("灰度模式已降级"))
            && event.message.contains(QStringLiteral("opencv_cvtcolor"))
            && event.message.contains(QStringLiteral("stable_manual"))) {
            foundDowngradeWarning = true;
            break;
        }
    }

    QVERIFY(foundDowngradeWarning);
}

QTEST_GUILESS_MAIN(InspectionWorkerTests)

#include "inspectionworker_tests.moc"
