#include <QtTest>

#include <opencv2/imgproc.hpp>

#include "logger/logmanager.h"
#include "vision/detectionworker.h"

namespace
{
DetectionRequest makeRequest(const cv::Mat &image)
{
    DetectionRequest request;
    request.inspectionId = QStringLiteral("inspection-test-001");
    request.frame.meta.captureId = QStringLiteral("capture-test-001");
    request.frame.meta.sourceType = InputSourceType::VideoFile;
    request.frame.meta.sourceName = QStringLiteral("sample.mp4");
    request.frame.meta.frameIndex = 12;
    request.frame.meta.capturedAt = QDateTime::currentDateTime();
    request.frame.image = image;
    request.visionParam.threshold = 128;
    request.visionParam.minArea = 50;
    request.visionParam.maxArea = 500000;
    return request;
}
} // namespace

class DetectionWorkerTests : public QObject
{
    Q_OBJECT

private slots:
    void emitsCompletedForValidFrame();
    void emitsFailedForInvalidFrame();
};

void DetectionWorkerTests::emitsCompletedForValidFrame()
{
    qRegisterMetaType<DetectionOutput>("DetectionOutput");

    LogManager logManager(nullptr, QStringLiteral("tests/logs"));
    DetectionWorker worker(&logManager);

    QSignalSpy completedSpy(&worker, &DetectionWorker::completed);
    QSignalSpy failedSpy(&worker, &DetectionWorker::failed);

    cv::Mat image(1080, 1920, CV_8UC3, cv::Scalar(255, 255, 255));
    cv::rectangle(image, cv::Rect(240, 320, 140, 180), cv::Scalar(0, 0, 0), cv::FILLED);

    worker.process(makeRequest(image));

    QCOMPARE(completedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);

    const QList<QVariant> arguments = completedSpy.takeFirst();
    QVERIFY(!arguments.isEmpty());
    const DetectionOutput output = qvariant_cast<DetectionOutput>(arguments.at(0));
    QCOMPARE(output.result.inspectionId, QStringLiteral("inspection-test-001"));
    QCOMPARE(output.request.frame.meta.captureId, QStringLiteral("capture-test-001"));
    QVERIFY(!output.result.canceled);
    QVERIFY(!output.annotatedImage.empty());
}

void DetectionWorkerTests::emitsFailedForInvalidFrame()
{
    LogManager logManager(nullptr, QStringLiteral("tests/logs"));
    DetectionWorker worker(&logManager);

    QSignalSpy completedSpy(&worker, &DetectionWorker::completed);
    QSignalSpy failedSpy(&worker, &DetectionWorker::failed);

    DetectionRequest request = makeRequest(cv::Mat{});
    request.frame.image = cv::Mat{};

    worker.process(request);

    QCOMPARE(completedSpy.count(), 0);
    QCOMPARE(failedSpy.count(), 1);

    const QList<QVariant> arguments = failedSpy.takeFirst();
    QCOMPARE(arguments.at(0).toString(), QStringLiteral("inspection-test-001"));
    QVERIFY(arguments.at(1).toString().contains(QStringLiteral("未提供有效的待检测帧")));
}

QTEST_GUILESS_MAIN(DetectionWorkerTests)

#include "detectionworker_tests.moc"
