#include <QtTest>

#include <opencv2/imgproc.hpp>

#include "domain/services/imageprocessor.h"

namespace
{
constexpr const char *kGrayModeEnvName = "VISION_ALLOW_UNSTABLE_OPENCV_GRAY";

struct ScopedEnvOverride
{
    explicit ScopedEnvOverride(const QByteArray &nextValue, bool enable)
        : hadOriginal(qEnvironmentVariableIsSet(kGrayModeEnvName))
        , originalValue(qgetenv(kGrayModeEnvName))
    {
        if (enable) {
            qputenv(kGrayModeEnvName, nextValue);
        } else {
            qunsetenv(kGrayModeEnvName);
        }
    }

    ~ScopedEnvOverride()
    {
        if (hadOriginal) {
            qputenv(kGrayModeEnvName, originalValue);
        } else {
            qunsetenv(kGrayModeEnvName);
        }
    }

    bool hadOriginal = false;
    QByteArray originalValue;
};
} // namespace

class ImageProcessorTests : public QObject
{
    Q_OBJECT

private slots:
    void returnsFailureForEmptyImage();
    void returnsOkWhenDefectDetectionIsDisabled();
    void reportsStableManualAsEffectiveGrayModeWhenExperimentalPathIsDisabled();
    void reportsOpenCvAsEffectiveGrayModeWhenExperimentalPathIsEnabled();
    void detectsDarkDefectOnLargeFrame();
    void appliesRoiAndOffsetsDefectRect();
    void supportsBgraInputWithManualGrayConversion();
};

void ImageProcessorTests::returnsFailureForEmptyImage()
{
    ImageProcessor processor;
    const InspectionResult result = processor.process(cv::Mat{}, Recipe{});

    QVERIFY(!result.isOk);
    QCOMPARE(result.defectCount, 0);
    QCOMPARE(result.failureReason, QStringLiteral("输入图像为空。"));
    QCOMPARE(result.summaryText, QStringLiteral("输入图像为空。"));
}

void ImageProcessorTests::returnsOkWhenDefectDetectionIsDisabled()
{
    cv::Mat image(640, 480, CV_8UC3, cv::Scalar(255, 255, 255));
    cv::rectangle(image, cv::Rect(100, 120, 80, 90), cv::Scalar(0, 0, 0), cv::FILLED);

    Recipe param;
    param.enableDefectDetection = false;

    ImageProcessor processor;
    const InspectionResult result = processor.process(image, param);

    QVERIFY(result.isOk);
    QCOMPARE(result.defectCount, 0);
    QVERIFY(result.defects.isEmpty());
    QCOMPARE(result.summaryText, QStringLiteral("AOI 缺陷检测项已关闭，本次按 OK 收敛。"));
}

void ImageProcessorTests::reportsStableManualAsEffectiveGrayModeWhenExperimentalPathIsDisabled()
{
    ScopedEnvOverride scopedEnv({}, false);

    Recipe param;
    param.grayConversionMode = GrayConversionMode::OpenCvCvtColor;

    QCOMPARE(
        ImageProcessor::effectiveGrayConversionMode(param),
        GrayConversionMode::StableManual);
}

void ImageProcessorTests::reportsOpenCvAsEffectiveGrayModeWhenExperimentalPathIsEnabled()
{
    ScopedEnvOverride scopedEnv("1", true);

    Recipe param;
    param.grayConversionMode = GrayConversionMode::OpenCvCvtColor;

    QCOMPARE(
        ImageProcessor::effectiveGrayConversionMode(param),
        GrayConversionMode::OpenCvCvtColor);
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
    const InspectionResult result = processor.process(image, param);

    QVERIFY(!result.isOk);
    QCOMPARE(result.defectCount, 1);
    QCOMPARE(result.defects.size(), 1);

    const QRect defectRect = result.defects.constFirst().boundingRect;
    QVERIFY(defectRect.contains(QPoint(220, 340)));
    QVERIFY(defectRect.width() >= 150);
    QVERIFY(defectRect.height() >= 170);
    QCOMPARE(result.summaryText, QStringLiteral("AOI 外观检测 NG：检测到 1 处缺陷。"));
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
    const InspectionResult result = processor.process(image, param);

    QVERIFY(!result.isOk);
    QCOMPARE(result.defectCount, 1);
    QCOMPARE(result.defects.size(), 1);

    const QRect defectRect = result.defects.constFirst().boundingRect;
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
    const InspectionResult result = processor.process(image, param);

    QVERIFY(!result.isOk);
    QCOMPARE(result.defectCount, 1);
    QCOMPARE(result.defects.size(), 1);

    const QRect defectRect = result.defects.constFirst().boundingRect;
    QVERIFY(defectRect.contains(QPoint(120, 150)));
    QVERIFY(defectRect.width() >= 75);
    QVERIFY(defectRect.height() >= 85);
}

QTEST_GUILESS_MAIN(ImageProcessorTests)

#include "imageprocessor_tests.moc"
