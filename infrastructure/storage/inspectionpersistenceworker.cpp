// 基础设施存储：inspectionpersistenceworker.cpp 负责记录落库与图片归档。
// 本文件承接巡检结果留痕出口，处理持久化副作用。
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

void InspectionPersistenceWorker::persist(const InspectionExecutionPayload &executionPayload)
{
    // 先构建结构化记录，再分别执行图片归档和数据库写入。
    InspectionRecord record = buildInspectionRecord(executionPayload);

    QString archiveMessage;
    const bool archiveSucceeded = archiveInspectionImages(executionPayload, record, &archiveMessage);

    QString recordError;
    const bool recordSaved = m_recordManager.saveRecord(record, &recordError);

    PersistenceResult persistenceResult;
    persistenceResult.inspectionId = executionPayload.result.inspectionId;
    persistenceResult.captureId = executionPayload.request.frame.meta.captureId;
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

QString InspectionPersistenceWorker::resolvedImageSaveDirectory(
    const InspectionExecutionPayload &executionPayload) const
{
    const QString configuredPath = executionPayload.request.recipe.imageSavePath.trimmed();
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

InspectionRecord InspectionPersistenceWorker::buildInspectionRecord(
    const InspectionExecutionPayload &executionPayload) const
{
    // 把检测输出映射为可持久化的结构化记录。
    InspectionRecord record;
    record.inspectionId = executionPayload.result.inspectionId;
    record.captureId = executionPayload.request.frame.meta.captureId;
    record.timestamp = utils::currentTimestamp();
    record.batchNo = QStringLiteral("LOCAL");
    record.recipeName = executionPayload.request.recipe.recipeName;
    record.sourceType = executionPayload.request.frame.meta.sourceType;
    record.sourcePath = executionPayload.request.frame.meta.sourcePath;
    record.sourceName = executionPayload.request.frame.meta.sourceName;
    record.frameIndex = executionPayload.request.frame.meta.frameIndex;
    record.isOk = executionPayload.result.isOk;
    record.defectCount = executionPayload.result.defectCount;
    record.processTimeMs = executionPayload.result.elapsedMs;
    record.summaryText =
        executionPayload.result.summaryText.isEmpty()
            ? executionPayload.result.failureReason
            : executionPayload.result.summaryText;
    return record;
}

bool InspectionPersistenceWorker::archiveInspectionImages(
    const InspectionExecutionPayload &executionPayload,
    InspectionRecord &record,
    QString *errorMessage) const
{
    // 归档目录按配置解析，首次写入时自动创建。
    const QDir saveDir(resolvedImageSaveDirectory(executionPayload));
    if (!QDir().mkpath(saveDir.absolutePath())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建图片归档目录：%1").arg(saveDir.absolutePath());
        }
        return false;
    }

    const QDateTime capturedAt = executionPayload.request.frame.meta.capturedAt.isValid()
                                     ? executionPayload.request.frame.meta.capturedAt
                                     : QDateTime::currentDateTime();
    const QString stamp = capturedAt.toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));

    const QString archivedSourcePath = saveDir.filePath(
        QStringLiteral("%1_%2_source.png")
            .arg(stamp, executionPayload.result.inspectionId));
    const QString archivedResultPath = saveDir.filePath(
        QStringLiteral("%1_%2_result.png")
            .arg(stamp, executionPayload.result.inspectionId));

    const bool saveSourceImage = executionPayload.request.recipe.saveSourceImage;
    const bool saveResultImage = executionPayload.request.recipe.saveResultImage || !saveSourceImage;

    // 原图和结果图按配方策略分别落盘，数据库仅保存最终落盘路径。
    if (saveSourceImage) {
        const QImage sourceImage = utils::matToQImage(executionPayload.request.frame.image);
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

        record.imagePath = archivedSourcePath;
    }

    if (saveResultImage) {
        const QImage resultImage = utils::matToQImage(executionPayload.annotatedImage);
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

        record.resultImagePath = archivedResultPath;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("source=%1, result=%2")
                            .arg(record.imagePath.isEmpty() ? QStringLiteral("disabled") : record.imagePath)
                            .arg(record.resultImagePath.isEmpty() ? QStringLiteral("disabled")
                                                                  : record.resultImagePath);
    }
    return true;
}
