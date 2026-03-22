#include "storage/inspectionpersistenceworker.h"

#include <utility>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include "common/constants.h"
#include "common/utils.h"

InspectionPersistenceWorker::InspectionPersistenceWorker(QString databasePath, QObject *parent)
    : QObject(parent)
    , m_recordManager(std::move(databasePath))
{
}

void InspectionPersistenceWorker::persist(const DetectionOutput &output)
{
    InspectionRecord record = buildInspectionRecord(output);

    QString archiveMessage;
    const bool archiveSucceeded = archiveDetectionImages(output, record, &archiveMessage);

    QString recordError;
    const bool recordSaved = m_recordManager.saveRecord(record, &recordError);

    PersistenceResult persistenceResult;
    persistenceResult.inspectionId = output.result.inspectionId;
    persistenceResult.captureId = output.request.frame.meta.captureId;
    persistenceResult.archiveSucceeded = archiveSucceeded;
    persistenceResult.recordSaved = recordSaved;
    persistenceResult.archivedSourcePath = record.imagePath;
    persistenceResult.resultImagePath = record.resultImagePath;
    persistenceResult.record = record;
    persistenceResult.archiveMessage = archiveMessage;
    persistenceResult.recordError = recordError;

    emit persistenceCompleted(persistenceResult);
}

QString InspectionPersistenceWorker::resolvedImageSaveDirectory(const DetectionOutput &output) const
{
    const QString configuredPath = output.request.visionParam.imageSavePath.trimmed();
    if (configuredPath.isEmpty()) {
        const QDir appDir(QCoreApplication::applicationDirPath());
        return appDir.filePath(QString::fromUtf8(constants::kImageArchiveDirectoryName));
    }

    const QFileInfo pathInfo(configuredPath);
    if (pathInfo.isAbsolute()) {
        return pathInfo.absoluteFilePath();
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    return appDir.filePath(configuredPath);
}

InspectionRecord InspectionPersistenceWorker::buildInspectionRecord(const DetectionOutput &output) const
{
    InspectionRecord record;
    record.inspectionId = output.result.inspectionId;
    record.captureId = output.request.frame.meta.captureId;
    record.timestamp = utils::currentTimestamp();
    record.batchNo = QStringLiteral("LOCAL");
    record.sourceType = output.request.frame.meta.sourceType;
    record.sourcePath = output.request.frame.meta.sourcePath;
    record.sourceName = output.request.frame.meta.sourceName;
    record.frameIndex = output.request.frame.meta.frameIndex;
    record.isOk = output.result.isOk;
    record.defectCount = output.result.defectCount;
    record.processTimeMs = output.result.processTimeMs;
    return record;
}

bool InspectionPersistenceWorker::archiveDetectionImages(
    const DetectionOutput &output,
    InspectionRecord &record,
    QString *errorMessage) const
{
    const QDir saveDir(resolvedImageSaveDirectory(output));
    if (!QDir().mkpath(saveDir.absolutePath())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建图片归档目录：%1").arg(saveDir.absolutePath());
        }
        return false;
    }

    const QDateTime capturedAt = output.request.frame.meta.capturedAt.isValid()
                                     ? output.request.frame.meta.capturedAt
                                     : QDateTime::currentDateTime();
    const QString stamp = capturedAt.toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));

    const QString archivedSourcePath = saveDir.filePath(
        QStringLiteral("%1_%2_source.png")
            .arg(stamp, output.result.inspectionId));
    const QString archivedResultPath = saveDir.filePath(
        QStringLiteral("%1_%2_result.png")
            .arg(stamp, output.result.inspectionId));

    const QImage sourceImage = utils::matToQImage(output.request.frame.image);
    if (sourceImage.isNull()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法转换原始帧用于归档。");
        }
        return false;
    }

    if (!sourceImage.save(archivedSourcePath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("原始图片归档失败：%1").arg(archivedSourcePath);
        }
        return false;
    }

    const QImage resultImage = utils::matToQImage(output.annotatedImage);
    if (resultImage.isNull()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法转换结果图用于归档。");
        }
        return false;
    }

    if (!resultImage.save(archivedResultPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("结果图片归档失败：%1").arg(archivedResultPath);
        }
        return false;
    }

    record.imagePath = archivedSourcePath;
    record.resultImagePath = archivedResultPath;
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("source=%1, result=%2")
                            .arg(record.imagePath)
                            .arg(record.resultImagePath);
    }
    return true;
}
