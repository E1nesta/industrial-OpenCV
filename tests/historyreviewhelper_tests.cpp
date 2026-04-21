#include <QtTest>

#include <QColor>
#include <QFile>
#include <QTemporaryDir>

#include "ui/historyreviewhelper.h"

namespace
{
QString writeImageFile(const QTemporaryDir &tempDir, const QString &fileName, const QColor &color)
{
    const QString path = tempDir.filePath(fileName);
    QImage image(48, 32, QImage::Format_RGB32);
    image.fill(color);
    image.save(path);
    return path;
}
} // namespace

class HistoryReviewHelperTests : public QObject
{
    Q_OBJECT

private slots:
    void loadsSourceImageAndAllowsReuse();
    void fallsBackToResultOnlyWhenSourceIsMissing();
    void fallsBackToResultOnlyWhenSourceIsUnreadable();
    void returnsNoImageWhenBothUnavailable();
};

void HistoryReviewHelperTests::loadsSourceImageAndAllowsReuse()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    InspectionRecord record;
    record.imagePath = writeImageFile(tempDir, QStringLiteral("source.png"), QColor(30, 60, 90));
    record.resultImagePath = writeImageFile(tempDir, QStringLiteral("result.png"), QColor(90, 60, 30));

    const HistoryReviewContent content = loadHistoryReviewContent(record);

    QVERIFY(content.hasDisplayableImage());
    QVERIFY(content.sourceImageReady);
    QVERIFY(content.resultImageReady);
    QVERIFY(content.canReuseAsInspectionInput());
    QCOMPARE(content.inspectionInputPath, record.imagePath);
    QCOMPARE(content.currentImageLabel, record.imagePath);
    QCOMPARE(content.statusMessage, QStringLiteral("已加载历史记录图像"));
}

void HistoryReviewHelperTests::fallsBackToResultOnlyWhenSourceIsMissing()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    InspectionRecord record;
    record.imagePath = tempDir.filePath(QStringLiteral("missing-source.png"));
    record.resultImagePath = writeImageFile(tempDir, QStringLiteral("result.png"), QColor(120, 80, 40));

    const HistoryReviewContent content = loadHistoryReviewContent(record);

    QVERIFY(content.hasDisplayableImage());
    QVERIFY(!content.sourceImageReady);
    QVERIFY(content.resultImageReady);
    QVERIFY(!content.canReuseAsInspectionInput());
    QCOMPARE(content.currentImageLabel, QStringLiteral("无原图，仅支持结果回看"));
    QCOMPARE(content.statusMessage, QStringLiteral("原图不可用，已降级为历史结果图回看"));
    QVERIFY(!content.warnings.isEmpty());
}

void HistoryReviewHelperTests::fallsBackToResultOnlyWhenSourceIsUnreadable()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString invalidSourcePath = tempDir.filePath(QStringLiteral("invalid-source.png"));
    QFile invalidSourceFile(invalidSourcePath);
    QVERIFY(invalidSourceFile.open(QIODevice::WriteOnly | QIODevice::Text));
    invalidSourceFile.write("not an image");
    invalidSourceFile.close();

    InspectionRecord record;
    record.imagePath = invalidSourcePath;
    record.resultImagePath = writeImageFile(tempDir, QStringLiteral("result.png"), QColor(180, 30, 60));

    const HistoryReviewContent content = loadHistoryReviewContent(record);

    QVERIFY(content.hasDisplayableImage());
    QVERIFY(!content.sourceImageReady);
    QVERIFY(content.resultImageReady);
    QVERIFY(!content.canReuseAsInspectionInput());
    QCOMPARE(content.statusMessage, QStringLiteral("原图不可用，已降级为历史结果图回看"));
    QVERIFY(content.warnings.contains(QStringLiteral("历史原图无法加载：%1").arg(invalidSourcePath)));
}

void HistoryReviewHelperTests::returnsNoImageWhenBothUnavailable()
{
    InspectionRecord record;
    record.imagePath = QStringLiteral("missing-source.png");
    record.resultImagePath = QStringLiteral("missing-result.png");

    const HistoryReviewContent content = loadHistoryReviewContent(record);

    QVERIFY(!content.hasDisplayableImage());
    QVERIFY(!content.sourceImageReady);
    QVERIFY(!content.resultImageReady);
    QCOMPARE(content.currentImageLabel, QStringLiteral("无可回看图像"));
    QCOMPARE(content.statusMessage, QStringLiteral("历史记录图像无法加载"));
    QVERIFY(!content.warnings.isEmpty());
}

QTEST_GUILESS_MAIN(HistoryReviewHelperTests)

#include "historyreviewhelper_tests.moc"
