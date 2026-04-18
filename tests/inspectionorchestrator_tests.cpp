#include <QtTest>

#include <QDateTime>
#include <QTemporaryDir>

#include <opencv2/core/mat.hpp>

#include "application/orchestrators/inspectionorchestrator.h"

namespace
{
CapturedFrame makeFrame(qint64 frameIndex = 7)
{
    CapturedFrame frame;
    frame.meta.captureId = QStringLiteral("capture-001");
    frame.meta.sourceType = InputSourceType::VideoFile;
    frame.meta.sourcePath = QStringLiteral("demo.mp4");
    frame.meta.sourceName = QStringLiteral("demo.mp4");
    frame.meta.frameIndex = frameIndex;
    frame.meta.capturedAt = QDateTime::currentDateTime();
    frame.image = cv::Mat(4, 5, CV_8UC3, cv::Scalar(10, 20, 30));
    return frame;
}

CaptureStatusSnapshot makePreviewStatus(
    InputSourceType type = InputSourceType::VideoFile,
    CaptureState state = CaptureState::Previewing)
{
    CaptureStatusSnapshot status;
    status.state = state;
    status.opened = (state == CaptureState::Previewing);
    status.source.type = type;
    status.source.sourceName = type == InputSourceType::Camera ? QStringLiteral("camera-0")
                                                               : QStringLiteral("demo.mp4");
    return status;
}

Recipe makeRecipe()
{
    Recipe recipe;
    recipe.threshold = 140;
    recipe.minArea = 15;
    recipe.maxArea = 600;
    recipe.roi = QRect(3, 4, 20, 30);
    recipe.enableMorphology = true;
    recipe.imageSavePath = QStringLiteral("data/test-images");
    return recipe;
}

QString createImageFile(QTemporaryDir *tempDir)
{
    const QString imagePath = tempDir->filePath(QStringLiteral("sample.png"));
    QImage image(12, 8, QImage::Format_RGB32);
    image.fill(qRgb(12, 34, 56));
    image.save(imagePath);
    return imagePath;
}
} // namespace

class InspectionOrchestratorTests : public QObject
{
    Q_OBJECT

private slots:
    void startInspectionFromFileRejectsWhenRunning();
    void startInspectionFromFileRejectsWhenCanceling();
    void startInspectionFromFileRejectsEmptyPath();
    void startInspectionFromFileRejectsUnreadableImage();
    void startInspectionFromFileBuildsTaskAndStartsSession();
    void startInspectionFromFrameRejectsWhenNotPreviewing();
    void startInspectionFromFrameRejectsInvalidFrame();
    void startInspectionFromFrameRejectsWhenRunning();
    void startInspectionFromFrameBuildsTaskAndClonesImage();
    void canStartContinuousInspectionValidatesPreconditions();
    void shouldTriggerContinuousInspectionHonorsGateConditions();
    void markContinuousInspectionTriggeredStoresFrameIndex();
};

void InspectionOrchestratorTests::startInspectionFromFileRejectsWhenRunning()
{
    InspectionOrchestrator orchestrator;
    InspectionSessionState state;
    InspectionTask task;
    QString errorMessage;

    state.beginInspection(QStringLiteral("inspection-001"));

    QVERIFY(!orchestrator.startInspectionFromFile(
        QStringLiteral("sample.png"), makeRecipe(), state, &task, &errorMessage));
    QCOMPARE(errorMessage, QStringLiteral("巡检进行中，请等待当前任务完成。"));
}

void InspectionOrchestratorTests::startInspectionFromFileRejectsWhenCanceling()
{
    InspectionOrchestrator orchestrator;
    InspectionSessionState state;
    InspectionTask task;
    QString errorMessage;

    state.beginInspection(QStringLiteral("inspection-001"));
    QVERIFY(state.requestCancel());

    QVERIFY(!orchestrator.startInspectionFromFile(
        QStringLiteral("sample.png"), makeRecipe(), state, &task, &errorMessage));
    QCOMPARE(errorMessage, QStringLiteral("正在取消上一个巡检任务，请稍后再试。"));
}

void InspectionOrchestratorTests::startInspectionFromFileRejectsEmptyPath()
{
    InspectionOrchestrator orchestrator;
    InspectionSessionState state;
    InspectionTask task;
    QString errorMessage;

    QVERIFY(!orchestrator.startInspectionFromFile(
        QString(), makeRecipe(), state, &task, &errorMessage));
    QCOMPARE(errorMessage, QStringLiteral("请先导入一张图片。"));
}

void InspectionOrchestratorTests::startInspectionFromFileRejectsUnreadableImage()
{
    InspectionOrchestrator orchestrator;
    InspectionSessionState state;
    InspectionTask task;
    QString errorMessage;
    const QString imagePath = QStringLiteral("Z:/definitely-missing-image.png");

    QVERIFY(!orchestrator.startInspectionFromFile(imagePath, makeRecipe(), state, &task, &errorMessage));
    QCOMPARE(errorMessage, QStringLiteral("无法读取待巡检图片：%1").arg(imagePath));
}

void InspectionOrchestratorTests::startInspectionFromFileBuildsTaskAndStartsSession()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    InspectionOrchestrator orchestrator;
    InspectionSessionState state;
    InspectionTask task;
    QString errorMessage;
    const Recipe recipe = makeRecipe();
    const QString imagePath = createImageFile(&tempDir);

    QVERIFY(orchestrator.startInspectionFromFile(imagePath, recipe, state, &task, &errorMessage));
    QVERIFY(errorMessage.isEmpty());
    QVERIFY(!task.inspectionId.isEmpty());
    QCOMPARE(state.activeInspectionId, task.inspectionId);
    QVERIFY(state.inspectionRunning);
    QCOMPARE(task.frame.meta.sourceType, InputSourceType::FileImage);
    QCOMPARE(task.frame.meta.sourcePath, imagePath);
    QCOMPARE(task.frame.meta.sourceName, QStringLiteral("sample.png"));
    QCOMPARE(task.frame.meta.frameIndex, 0);
    QVERIFY(task.frame.isValid());
    QCOMPARE(task.recipe.threshold, recipe.threshold);
    QCOMPARE(task.recipe.minArea, recipe.minArea);
    QCOMPARE(task.recipe.maxArea, recipe.maxArea);
    QCOMPARE(task.recipe.roi, recipe.roi);
    QCOMPARE(task.recipe.enableMorphology, recipe.enableMorphology);
    QCOMPARE(task.recipe.imageSavePath, recipe.imageSavePath);
}

void InspectionOrchestratorTests::startInspectionFromFrameRejectsWhenNotPreviewing()
{
    InspectionOrchestrator orchestrator;
    InspectionSessionState state;
    InspectionTask task;
    QString errorMessage;

    QVERIFY(!orchestrator.startInspectionFromFrame(
        makeFrame(), makePreviewStatus(InputSourceType::VideoFile, CaptureState::Idle), makeRecipe(), state, &task,
        &errorMessage));
    QCOMPARE(errorMessage, QStringLiteral("请先启动预览并保持输入源处于预览状态。"));
}

void InspectionOrchestratorTests::startInspectionFromFrameRejectsInvalidFrame()
{
    InspectionOrchestrator orchestrator;
    InspectionSessionState state;
    InspectionTask task;
    QString errorMessage;
    CapturedFrame frame;

    QVERIFY(!orchestrator.startInspectionFromFrame(
        frame, makePreviewStatus(), makeRecipe(), state, &task, &errorMessage));
    QCOMPARE(errorMessage, QStringLiteral("当前没有可用帧，请先启动预览。"));
}

void InspectionOrchestratorTests::startInspectionFromFrameRejectsWhenRunning()
{
    InspectionOrchestrator orchestrator;
    InspectionSessionState state;
    InspectionTask task;
    QString errorMessage;

    state.beginInspection(QStringLiteral("inspection-001"));

    QVERIFY(!orchestrator.startInspectionFromFrame(
        makeFrame(), makePreviewStatus(), makeRecipe(), state, &task, &errorMessage));
    QCOMPARE(errorMessage, QStringLiteral("巡检进行中，请等待当前任务完成。"));
}

void InspectionOrchestratorTests::startInspectionFromFrameBuildsTaskAndClonesImage()
{
    InspectionOrchestrator orchestrator;
    InspectionSessionState state;
    InspectionTask task;
    QString errorMessage;
    const Recipe recipe = makeRecipe();
    CapturedFrame frame = makeFrame();
    const cv::Vec3b originalPixel = frame.image.at<cv::Vec3b>(0, 0);

    QVERIFY(orchestrator.startInspectionFromFrame(
        frame, makePreviewStatus(), recipe, state, &task, &errorMessage));
    QVERIFY(errorMessage.isEmpty());
    QVERIFY(!task.inspectionId.isEmpty());
    QCOMPARE(state.activeInspectionId, task.inspectionId);
    QVERIFY(state.inspectionRunning);
    QCOMPARE(task.frame.meta.captureId, QStringLiteral("capture-001"));
    QCOMPARE(task.frame.meta.frameIndex, 7);
    QCOMPARE(task.recipe.threshold, recipe.threshold);
    QVERIFY(task.frame.image.data != frame.image.data);
    QCOMPARE(task.frame.image.at<cv::Vec3b>(0, 0), originalPixel);

    frame.image.setTo(cv::Scalar(99, 88, 77));
    QCOMPARE(task.frame.image.at<cv::Vec3b>(0, 0), originalPixel);
}

void InspectionOrchestratorTests::canStartContinuousInspectionValidatesPreconditions()
{
    InspectionOrchestrator orchestrator;
    InspectionSessionState state;
    QString errorMessage;

    QVERIFY(!orchestrator.canStartContinuousInspection(
        makePreviewStatus(InputSourceType::FileImage), makeFrame(), state, &errorMessage));
    QCOMPARE(errorMessage, QStringLiteral("连续巡检仅支持视频文件或摄像头预览。"));

    QVERIFY(!orchestrator.canStartContinuousInspection(
        makePreviewStatus(InputSourceType::VideoFile, CaptureState::Idle), makeFrame(), state, &errorMessage));
    QCOMPARE(errorMessage, QStringLiteral("开启连续巡检失败：请先启动预览。"));

    CapturedFrame invalidFrame;
    QVERIFY(!orchestrator.canStartContinuousInspection(
        makePreviewStatus(), invalidFrame, state, &errorMessage));
    QCOMPARE(errorMessage, QStringLiteral("开启连续巡检失败：当前没有可用帧，请稍候再试。"));

    QVERIFY(orchestrator.canStartContinuousInspection(
        makePreviewStatus(), makeFrame(), state, &errorMessage));
    QVERIFY(errorMessage.isEmpty());
}

void InspectionOrchestratorTests::shouldTriggerContinuousInspectionHonorsGateConditions()
{
    InspectionOrchestrator orchestrator;
    InspectionSessionState state;
    const CaptureStatusSnapshot previewStatus = makePreviewStatus();
    const CapturedFrame frame = makeFrame(12);

    QVERIFY(!orchestrator.shouldTriggerContinuousInspection(previewStatus, frame, state));

    QVERIFY(state.startContinuousInspection());
    QVERIFY(orchestrator.shouldTriggerContinuousInspection(previewStatus, frame, state));

    state.beginInspection(QStringLiteral("inspection-001"));
    QVERIFY(!orchestrator.shouldTriggerContinuousInspection(previewStatus, frame, state));
    state.abortInspection();

    state.beginInspection(QStringLiteral("inspection-002"));
    QVERIFY(state.requestCancel());
    QVERIFY(!orchestrator.shouldTriggerContinuousInspection(previewStatus, frame, state));
    state.abortInspection();

    QVERIFY(!orchestrator.shouldTriggerContinuousInspection(
        makePreviewStatus(InputSourceType::VideoFile, CaptureState::Idle), frame, state));

    CapturedFrame invalidFrame;
    QVERIFY(!orchestrator.shouldTriggerContinuousInspection(previewStatus, invalidFrame, state));

    orchestrator.markContinuousInspectionTriggered(frame, state);
    QVERIFY(!orchestrator.shouldTriggerContinuousInspection(previewStatus, frame, state));

    QVERIFY(orchestrator.shouldTriggerContinuousInspection(previewStatus, makeFrame(13), state));
}

void InspectionOrchestratorTests::markContinuousInspectionTriggeredStoresFrameIndex()
{
    InspectionOrchestrator orchestrator;
    InspectionSessionState state;

    orchestrator.markContinuousInspectionTriggered(makeFrame(99), state);

    QCOMPARE(state.lastContinuousInspectionFrameIndex, 99);
    QVERIFY(state.hasHandledFrame(99));
}

QTEST_GUILESS_MAIN(InspectionOrchestratorTests)

#include "inspectionorchestrator_tests.moc"
