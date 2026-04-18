#include <QtTest>

#include <QDateTime>
#include <QFileInfo>
#include <QTemporaryDir>

#include <opencv2/imgproc.hpp>

#include "infrastructure/storage/inspectionpersistenceworker.h"

namespace
{
InspectionOutput makeOutput(const QString &imageSavePath)
{
    InspectionOutput output;
    output.request.inspectionId = QStringLiteral("inspection-persist-001");
    output.request.frame.meta.captureId = QStringLiteral("capture-persist-001");
    output.request.frame.meta.sourceType = InputSourceType::VideoFile;
    output.request.frame.meta.sourcePath = QStringLiteral("G:/video/sample.mp4");
    output.request.frame.meta.sourceName = QStringLiteral("sample.mp4");
    output.request.frame.meta.frameIndex = 7;
    output.request.frame.meta.capturedAt = QDateTime::currentDateTime();
    output.request.recipe.imageSavePath = imageSavePath;
    output.request.frame.image = cv::Mat(720, 1280, CV_8UC3, cv::Scalar(240, 240, 240));

    output.result.inspectionId = output.request.inspectionId;
    output.result.frameMeta = output.request.frame.meta;
    output.result.isOk = false;
    output.result.defectCount = 1;
    output.result.processTimeMs = 15.5;
    output.result.message = QStringLiteral("检测到 1 处缺陷。");
    output.result.defectRects.append(QRect(120, 160, 90, 110));

    output.annotatedImage = output.request.frame.image.clone();
    cv::rectangle(output.annotatedImage, cv::Rect(120, 160, 90, 110), cv::Scalar(0, 0, 255), 2);
    return output;
}
} // namespace

class PersistenceWorkerTests : public QObject
{
    Q_OBJECT

private slots:
    void archivesImagesAndSavesRecord();
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

    worker.persist(makeOutput(imageSavePath));

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
}

QTEST_GUILESS_MAIN(PersistenceWorkerTests)

#include "persistenceworker_tests.moc"
