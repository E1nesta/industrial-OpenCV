#include <QtTest>

#include <opencv2/imgproc.hpp>

#include "common/utils.h"

class UtilsTests : public QObject
{
    Q_OBJECT

private slots:
    void convertsBgrMatToQImage();
    void buildsPreviewImageWithDownsampleAndRoi();
    void drawsDetectionOverlay();
};

void UtilsTests::convertsBgrMatToQImage()
{
    cv::Mat image(1920, 1080, CV_8UC3, cv::Scalar(10, 20, 30));

    const QImage converted = utils::matToQImage(image);

    QVERIFY(!converted.isNull());
    QCOMPARE(converted.width(), image.cols);
    QCOMPARE(converted.height(), image.rows);
    QCOMPARE(converted.format(), QImage::Format_BGR888);
}

void UtilsTests::buildsPreviewImageWithDownsampleAndRoi()
{
    cv::Mat image(1920, 1080, CV_8UC3, cv::Scalar(32, 64, 96));

    VisionParam param;
    param.roi = QRect(100, 200, 240, 300);

    const QImage preview = utils::buildPreviewImage(image, param, 960);

    QVERIFY(!preview.isNull());
    QVERIFY(std::max(preview.width(), preview.height()) <= 960);
}

void UtilsTests::drawsDetectionOverlay()
{
    cv::Mat image(720, 1280, CV_8UC3, cv::Scalar(0, 0, 0));
    DetectResult result;
    result.isOk = false;
    result.defectCount = 1;
    result.processTimeMs = 12.5;
    result.defectRects.append(QRect(100, 120, 200, 160));

    const cv::Mat annotated = utils::drawDetectionOverlay(image, result);

    QVERIFY(!annotated.empty());
    QCOMPARE(annotated.cols, image.cols);
    QCOMPARE(annotated.rows, image.rows);
    QCOMPARE(annotated.type(), image.type());
}

QTEST_GUILESS_MAIN(UtilsTests)

#include "utils_tests.moc"
