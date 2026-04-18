#include <QtTest>

#include <opencv2/imgproc.hpp>

#include "domain/services/imageprocessor.h"

class ImageProcessorTests : public QObject
{
    Q_OBJECT

private slots:
    void returnsFailureForEmptyImage();
    void detectsDarkDefectOnLargeFrame();
    void appliesRoiAndOffsetsDefectRect();
    void supportsBgraInputWithManualGrayConversion();
};

void ImageProcessorTests::returnsFailureForEmptyImage()
{
    ImageProcessor processor;
    const InspectionResult result = processor.process(cv::Mat{}, Recipe{}, nullptr);

    QVERIFY(!result.isOk);
    QVERIFY(!result.canceled);
    QCOMPARE(result.defectCount, 0);
    QCOMPARE(result.message, QStringLiteral("输入图像为空。"));
}

void ImageProcessorTests::detectsDarkDefectOnLargeFrame()
{
    cv::Mat image(1920, 1080, CV_8UC3, cv::Scalar(255, 255, 255));
    cv::rectangle(image, cv::Rect(220, 340, 160, 180), cv::Scalar(0, 0, 0), cv::FILLED);

    Recipe param;
    param.threshold = 128;
    param.minArea = 100;
    param.maxArea = 200000;

    ImageProcessor processor;
    const InspectionResult result = processor.process(image, param, nullptr);

    QVERIFY(!result.canceled);
    QVERIFY(!result.isOk);
    QCOMPARE(result.defectCount, 1);
    QCOMPARE(result.defectRects.size(), 1);

    const QRect defectRect = result.defectRects.constFirst();
    QVERIFY(defectRect.contains(QPoint(220, 340)));
    QVERIFY(defectRect.width() >= 150);
    QVERIFY(defectRect.height() >= 170);
}

void ImageProcessorTests::appliesRoiAndOffsetsDefectRect()
{
    cv::Mat image(800, 600, CV_8UC3, cv::Scalar(255, 255, 255));
    cv::rectangle(image, cv::Rect(180, 220, 90, 110), cv::Scalar(0, 0, 0), cv::FILLED);

    Recipe param;
    param.threshold = 128;
    param.minArea = 50;
    param.maxArea = 50000;
    param.roi = QRect(120, 180, 240, 220);

    ImageProcessor processor;
    const InspectionResult result = processor.process(image, param, nullptr);

    QVERIFY(!result.canceled);
    QVERIFY(!result.isOk);
    QCOMPARE(result.defectCount, 1);
    QCOMPARE(result.defectRects.size(), 1);

    const QRect defectRect = result.defectRects.constFirst();
    QVERIFY(defectRect.left() >= 175 && defectRect.left() <= 185);
    QVERIFY(defectRect.top() >= 215 && defectRect.top() <= 225);
    QVERIFY(defectRect.width() >= 85);
    QVERIFY(defectRect.height() >= 105);
}

void ImageProcessorTests::supportsBgraInputWithManualGrayConversion()
{
    cv::Mat image(480, 640, CV_8UC4, cv::Scalar(255, 255, 255, 255));
    cv::rectangle(image, cv::Rect(120, 150, 80, 90), cv::Scalar(0, 0, 0, 255), cv::FILLED);

    Recipe param;
    param.threshold = 128;
    param.minArea = 50;
    param.maxArea = 20000;
    param.grayConversionMode = GrayConversionMode::StableManual;

    ImageProcessor processor;
    const InspectionResult result = processor.process(image, param, nullptr);

    QVERIFY(!result.canceled);
    QVERIFY(!result.isOk);
    QCOMPARE(result.defectCount, 1);
    QCOMPARE(result.defectRects.size(), 1);

    const QRect defectRect = result.defectRects.constFirst();
    QVERIFY(defectRect.contains(QPoint(120, 150)));
    QVERIFY(defectRect.width() >= 75);
    QVERIFY(defectRect.height() >= 85);
}

QTEST_GUILESS_MAIN(ImageProcessorTests)

#include "imageprocessor_tests.moc"
