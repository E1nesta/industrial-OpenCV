// 表现层辅助：historyreviewhelper 负责收敛历史记录回看所需内容。
// 本文件只处理回看资源解析，不直接操作控件。
#include "ui/historyreviewhelper.h"

#include <QFileInfo>

namespace
{
void appendMissingFileWarning(
    const QString &label,
    const QString &path,
    QStringList *warnings)
{
    if (warnings == nullptr || path.trimmed().isEmpty()) {
        return;
    }

    warnings->append(QStringLiteral("历史%1不存在：%2").arg(label, path));
}

void appendUnreadableFileWarning(
    const QString &label,
    const QString &path,
    QStringList *warnings)
{
    if (warnings == nullptr || path.trimmed().isEmpty()) {
        return;
    }

    warnings->append(QStringLiteral("历史%1无法加载：%2").arg(label, path));
}
} // namespace

HistoryReviewContent loadHistoryReviewContent(const InspectionRecord &record)
{
    HistoryReviewContent content;

    const QString sourcePath = record.imagePath.trimmed();
    const QString resultPath = record.resultImagePath.trimmed();

    if (!sourcePath.isEmpty()) {
        const QFileInfo sourceInfo(sourcePath);
        if (sourceInfo.exists()) {
            const QImage sourceImage(sourcePath);
            if (sourceImage.isNull()) {
                appendUnreadableFileWarning(QStringLiteral("原图"), sourcePath, &content.warnings);
            } else {
                content.sourceImage = sourceImage;
                content.sourceImageReady = true;
                content.inspectionInputPath = sourcePath;
                content.inspectionInputName = sourceInfo.fileName();
                content.currentImageLabel = sourcePath;
            }
        } else {
            appendMissingFileWarning(QStringLiteral("原图"), sourcePath, &content.warnings);
        }
    }

    if (!resultPath.isEmpty()) {
        const QFileInfo resultInfo(resultPath);
        if (resultInfo.exists()) {
            const QImage resultImage(resultPath);
            if (resultImage.isNull()) {
                appendUnreadableFileWarning(QStringLiteral("结果图"), resultPath, &content.warnings);
            } else {
                content.resultImage = resultImage;
                content.resultImageReady = true;
            }
        } else {
            appendMissingFileWarning(QStringLiteral("结果图"), resultPath, &content.warnings);
        }
    }

    if (content.sourceImageReady) {
        content.statusMessage = QStringLiteral("已加载历史记录图像");
        return content;
    }

    if (content.resultImageReady) {
        content.currentImageLabel = QStringLiteral("无原图，仅支持结果回看");
        content.statusMessage = QStringLiteral("原图不可用，已降级为历史结果图回看");
        return content;
    }

    if (content.warnings.isEmpty()) {
        content.warnings.append(
            QStringLiteral("历史记录图片不存在：source=%1 result=%2")
                .arg(record.imagePath, record.resultImagePath));
    }

    content.currentImageLabel = QStringLiteral("无可回看图像");
    content.statusMessage = QStringLiteral("历史记录图像无法加载");
    return content;
}
