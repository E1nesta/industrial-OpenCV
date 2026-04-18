#include "infrastructure/storage/inspectionpersistenceworker.h"

#include <utility>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include "common/config/constants.h"
#include "common/utils/utils.h"

InspectionPersistenceWorker::InspectionPersistenceWorker(QString databasePath, QObject *parent)
    : QObject(parent)
    , m_recordManager(std::move(databasePath))
{
    // 记录管理器在 worker 线程内使用，避免主线程阻塞持久化 I/O。
}

void InspectionPersistenceWorker::persist(const InspectionOutput &output)
{
    // 先构建结构化记录，再分别执行图片归档和数据库写入。
    InspectionRecord record = buildInspectionRecord(output);

    QString archiveMessage;
    const bool archiveSucceeded = archiveInspectionImages(output, record, &archiveMessage);

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

    // 持久化完成后统一回传结果，控制层据此刷新状态与日志。
    emit persistenceCompleted(persistenceResult);
}

QString InspectionPersistenceWorker::resolvedImageSaveDirectory(const InspectionOutput &output) const
{
    const QString configuredPath = output.request.recipe.imageSavePath.trimmed();
    if (configuredPath.isEmpty()) {
        const QDir appDir(QCoreApplication::applicationDirPath());
        return appDir.filePath(QString::fromUtf8(constants::kImageArchiveDirectoryName));
    }

    const QFileInfo pathInfo(configuredPath);
    // 相对路径按程序目录解析，保持部署目录可迁移。
    if (pathInfo.isAbsolute()) {
        return pathInfo.absoluteFilePath();
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    return appDir.filePath(configuredPath);
}

InspectionRecord InspectionPersistenceWorker::buildInspectionRecord(const InspectionOutput &output) const
{
    // 把检测输出映射为可持久化的结构化记录。
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

bool InspectionPersistenceWorker::archiveInspectionImages(
    const InspectionOutput &output,
    InspectionRecord &record,
    QString *errorMessage) const
{
    // 归档目录按配置解析，首次写入时自动创建。
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

    // 原图和结果图分别落盘，数据库仅保存路径索引。
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
