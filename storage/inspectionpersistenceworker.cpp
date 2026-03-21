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

void InspectionPersistenceWorker::persist(
    const DetectResult &result,
    const QImage &resultImage,
    const VisionParam &param)
{
    InspectionRecord record = buildInspectionRecord(result);

    QString archiveMessage;
    const bool archiveSucceeded =
        archiveDetectionImages(result, resultImage, param, record, &archiveMessage);

    QString recordError;
    const bool recordSaved = m_recordManager.saveRecord(record, &recordError);

    emit persistenceCompleted(record, archiveSucceeded, archiveMessage, recordSaved, recordError);
}

QString InspectionPersistenceWorker::resolvedImageSaveDirectory(const VisionParam &param) const
{
    const QString configuredPath = param.imageSavePath.trimmed();
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

InspectionRecord InspectionPersistenceWorker::buildInspectionRecord(const DetectResult &result) const
{
    InspectionRecord record;
    record.inspectionId = result.inspectionId;
    record.timestamp = utils::currentTimestamp();
    record.batchNo = QStringLiteral("LOCAL");
    record.isOk = result.isOk;
    record.defectCount = result.defectCount;
    record.processTimeMs = result.processTimeMs;
    record.imagePath = result.imagePath;
    return record;
}

bool InspectionPersistenceWorker::archiveDetectionImages(
    const DetectResult &result,
    const QImage &resultImage,
    const VisionParam &param,
    InspectionRecord &record,
    QString *errorMessage) const
{
    const QDir saveDir(resolvedImageSaveDirectory(param));
    if (!QDir().mkpath(saveDir.absolutePath())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建图片归档目录：%1").arg(saveDir.absolutePath());
        }
        return false;
    }

    const QFileInfo sourceInfo(result.imagePath);
    const QString baseName =
        sourceInfo.completeBaseName().isEmpty() ? QStringLiteral("inspection") : sourceInfo.completeBaseName();
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString sourceSuffix = sourceInfo.suffix().isEmpty() ? QStringLiteral("png") : sourceInfo.suffix().toLower();

    const QString archivedSourcePath = saveDir.filePath(
        QStringLiteral("%1_%2_source.%3").arg(stamp, baseName, sourceSuffix));
    const QString archivedResultPath = saveDir.filePath(
        QStringLiteral("%1_%2_result.png").arg(stamp, baseName));

    const QImage sourceImage(result.imagePath);
    if (sourceImage.isNull()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法读取原始图片用于归档：%1").arg(result.imagePath);
        }
        return false;
    }

    if (!sourceImage.save(archivedSourcePath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("原始图片归档失败：%1").arg(archivedSourcePath);
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
