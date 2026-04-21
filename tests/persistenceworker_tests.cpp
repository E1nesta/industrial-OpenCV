#include <QtTest>

#include <QDateTime>
#include <QFileInfo>
#include <QTemporaryDir>

#include <opencv2/imgproc.hpp>

#include "infrastructure/storage/inspectionpersistenceworker.h"

namespace
{
InspectionExecutionPayload makeExecutionPayload(const QString &imageSavePath)
{
    InspectionExecutionPayload executionPayload;
    executionPayload.request.inspectionId = QStringLiteral("inspection-persist-001");
    executionPayload.request.frame.meta.captureId = QStringLiteral("capture-persist-001");
    executionPayload.request.frame.meta.sourceType = InputSourceType::VideoFile;
    executionPayload.request.frame.meta.sourcePath = QStringLiteral("G:/video/sample.mp4");
    executionPayload.request.frame.meta.sourceName = QStringLiteral("sample.mp4");
    executionPayload.request.frame.meta.frameIndex = 7;
    executionPayload.request.frame.meta.capturedAt = QDateTime::currentDateTime();
    executionPayload.request.recipe.recipeName = QStringLiteral("aoi-default");
    executionPayload.request.recipe.imageSavePath = imageSavePath;
    executionPayload.request.frame.image = cv::Mat(720, 1280, CV_8UC3, cv::Scalar(240, 240, 240));

    executionPayload.result.inspectionId = executionPayload.request.inspectionId;
    executionPayload.result.frameMeta = executionPayload.request.frame.meta;
    executionPayload.result.isOk = false;
    executionPayload.result.defectCount = 1;
    executionPayload.result.elapsedMs = 15.5;
    executionPayload.result.summaryText = QStringLiteral("AOI 外观检测 NG：检测到 1 处缺陷。");
    DefectItem defect;
    defect.boundingRect = QRect(120, 160, 90, 110);
    defect.area = 9900.0;
    defect.category = QStringLiteral("blob_defect");
    defect.description = QStringLiteral("AOI 外观缺陷候选区域");
    executionPayload.result.defects.append(defect);

    executionPayload.annotatedImage = executionPayload.request.frame.image.clone();
    cv::rectangle(
        executionPayload.annotatedImage,
        cv::Rect(120, 160, 90, 110),
        cv::Scalar(0, 0, 255),
        2);
    return executionPayload;
}
} // namespace

class PersistenceWorkerTests : public QObject
{
    Q_OBJECT

private slots:
    void archivesImagesAndSavesRecord();
    void fallsBackToSavingResultImageWhenArchiveFlagsAreBothDisabled();
};

void PersistenceWorkerTests::archivesImagesAndSavesRecord()
{
    qRegisterMetaType<PersistenceResult>("PersistenceResult");

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString databasePath = tempDir.filePath(QStringLiteral("inspection.db"));
    const QString imageSavePath = tempDir.filePath(QStringLiteral("images"));

    InspectionPersistenceWorker worker(databasePath);
    QSignalSpy completedSpy(&worker, &InspectionPersistenceWorker::persistenceCompleted);

    worker.persist(makeExecutionPayload(imageSavePath));

    QCOMPARE(completedSpy.count(), 1);
    const QList<QVariant> arguments = completedSpy.takeFirst();
    const PersistenceResult result = qvariant_cast<PersistenceResult>(arguments.at(0));

    QCOMPARE(result.inspectionId, QStringLiteral("inspection-persist-001"));
    QCOMPARE(result.captureId, QStringLiteral("capture-persist-001"));
    QVERIFY(result.archiveSucceeded);
    QVERIFY(result.recordSaved);
    QVERIFY(QFileInfo::exists(result.archivedSourcePath));
    QVERIFY(QFileInfo::exists(result.resultImagePath));
    QVERIFY(QFileInfo::exists(databasePath));
    QCOMPARE(result.record.sourceName, QStringLiteral("sample.mp4"));
    QCOMPARE(result.record.sourceType, InputSourceType::VideoFile);
    QCOMPARE(result.record.frameIndex, 7);
    QCOMPARE(result.record.defectCount, 1);
    QCOMPARE(result.record.recipeName, QStringLiteral("aoi-default"));
    QCOMPARE(result.record.summaryText, QStringLiteral("AOI 外观检测 NG：检测到 1 处缺陷。"));
}

void PersistenceWorkerTests::fallsBackToSavingResultImageWhenArchiveFlagsAreBothDisabled()
{
    qRegisterMetaType<PersistenceResult>("PersistenceResult");

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString databasePath = tempDir.filePath(QStringLiteral("inspection.db"));
    const QString imageSavePath = tempDir.filePath(QStringLiteral("images"));

    InspectionExecutionPayload executionPayload = makeExecutionPayload(imageSavePath);
    executionPayload.request.recipe.saveSourceImage = false;
    executionPayload.request.recipe.saveResultImage = false;

    InspectionPersistenceWorker worker(databasePath);
    QSignalSpy completedSpy(&worker, &InspectionPersistenceWorker::persistenceCompleted);

    worker.persist(executionPayload);

    QCOMPARE(completedSpy.count(), 1);
    const QList<QVariant> arguments = completedSpy.takeFirst();
    const PersistenceResult result = qvariant_cast<PersistenceResult>(arguments.at(0));

    QVERIFY(result.archiveSucceeded);
    QVERIFY(result.recordSaved);
    QVERIFY(result.record.imagePath.isEmpty());
    QVERIFY(!result.record.resultImagePath.isEmpty());
    QVERIFY(QFileInfo::exists(result.record.resultImagePath));
}

QTEST_GUILESS_MAIN(PersistenceWorkerTests)

#include "persistenceworker_tests.moc"
