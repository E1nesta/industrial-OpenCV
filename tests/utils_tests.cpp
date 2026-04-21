#include <QtTest>

#include <opencv2/imgproc.hpp>

#include "application/previewrenderer.h"
#include "common/utils/utils.h"
#include "infrastructure/vision/inspectionoverlayrenderer.h"

class UtilsTests : public QObject
{
    Q_OBJECT

private slots:
    void convertsBgrMatToQImage();
    void convertsQImageToBgrMat();
    void convertsQImageToBgrMatWithExpectedChannelOrder();
    void buildsPreviewImageWithDownsampleAndRoi();
    void drawsInspectionOverlay();
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

void UtilsTests::convertsQImageToBgrMat()
{
    QImage image(320, 240, QImage::Format_RGB32);
    image.fill(qRgb(12, 34, 56));

    const cv::Mat converted = utils::qImageToMat(image);

    QVERIFY(!converted.empty());
    QCOMPARE(converted.cols, image.width());
    QCOMPARE(converted.rows, image.height());
    QCOMPARE(converted.type(), CV_8UC3);
}

void UtilsTests::convertsQImageToBgrMatWithExpectedChannelOrder()
{
    QImage image(2, 1, QImage::Format_RGBA8888);
    image.setPixelColor(0, 0, QColor(200, 150, 100, 255));
    image.setPixelColor(1, 0, QColor(30, 20, 10, 255));

    const cv::Mat converted = utils::qImageToMat(image);

    QVERIFY(!converted.empty());
    QCOMPARE(converted.type(), CV_8UC3);

    const cv::Vec3b firstPixel = converted.at<cv::Vec3b>(0, 0);
    QCOMPARE(static_cast<int>(firstPixel[0]), 100);
    QCOMPARE(static_cast<int>(firstPixel[1]), 150);
    QCOMPARE(static_cast<int>(firstPixel[2]), 200);

    const cv::Vec3b secondPixel = converted.at<cv::Vec3b>(0, 1);
    QCOMPARE(static_cast<int>(secondPixel[0]), 10);
    QCOMPARE(static_cast<int>(secondPixel[1]), 20);
    QCOMPARE(static_cast<int>(secondPixel[2]), 30);
}

void UtilsTests::buildsPreviewImageWithDownsampleAndRoi()
{
    cv::Mat image(1920, 1080, CV_8UC3, cv::Scalar(32, 64, 96));

    Recipe param;
    param.roi = QRect(100, 200, 240, 300);

    const QImage preview = previewrenderer::buildPreviewImage(image, param, 960);

    QVERIFY(!preview.isNull());
    QVERIFY(std::max(preview.width(), preview.height()) <= 960);
}

void UtilsTests::drawsInspectionOverlay()
{
    cv::Mat image(720, 1280, CV_8UC3, cv::Scalar(0, 0, 0));
    InspectionResult result;
    result.isOk = false;
    result.defectCount = 1;
    result.elapsedMs = 12.5;
    DefectItem defect;
    defect.boundingRect = QRect(100, 120, 200, 160);
    defect.area = 32000.0;
    result.defects.append(defect);

    const cv::Mat annotated = inspectionoverlayrenderer::drawInspectionOverlay(image, result);

    QVERIFY(!annotated.empty());
    QCOMPARE(annotated.cols, image.cols);
    QCOMPARE(annotated.rows, image.rows);
    QCOMPARE(annotated.type(), image.type());
}

QTEST_GUILESS_MAIN(UtilsTests)

#include "utils_tests.moc"
