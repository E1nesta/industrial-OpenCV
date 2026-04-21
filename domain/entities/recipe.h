// 领域实体：Recipe 定义巡检配方参数。
// 该对象用于统一描述阈值、ROI、图像处理与归档配置。
#pragma once

#include <QMetaType>
#include <QRect>
#include <QString>

enum class GrayConversionMode
{
    // 手工稳定灰度转换。
    StableManual,
    // 使用 OpenCV cvtColor 灰度转换。
    OpenCvCvtColor
};

inline QString grayConversionModeToString(GrayConversionMode mode)
{
    // 灰度模式转字符串，供配置持久化使用。
    switch (mode) {
    case GrayConversionMode::OpenCvCvtColor:
        return QStringLiteral("opencv_cvtcolor");
    case GrayConversionMode::StableManual:
    default:
        return QStringLiteral("stable_manual");
    }
}

inline GrayConversionMode grayConversionModeFromString(const QString &value)
{
    // 字符串转灰度模式，供配置加载回填使用。
    if (value.compare(QStringLiteral("opencv_cvtcolor"), Qt::CaseInsensitive) == 0) {
        return GrayConversionMode::OpenCvCvtColor;
    }

    return GrayConversionMode::StableManual;
}

struct Recipe
{
    // 配方名称。
    QString recipeName = QStringLiteral("default-aoi");
    // 是否启用 AOI 外观缺陷检测项。
    bool enableDefectDetection = true;
    // 二值化阈值。
    int threshold = 128;
    // 缺陷最小面积。
    int minArea = 10;
    // 缺陷最大面积。
    int maxArea = 1000;
    // 检测区域。
    QRect roi;
    // 是否启用形态学处理。
    bool enableMorphology = false;
    // 是否保存原图。
    bool saveSourceImage = true;
    // 是否保存结果图。
    bool saveResultImage = true;
    // 是否允许发送 TCP 结果。
    bool enableTcpResult = true;
    // 灰度转换模式。
    GrayConversionMode grayConversionMode = GrayConversionMode::StableManual;
    // 图片归档目录。
    QString imageSavePath = QStringLiteral("data/images");
};

Q_DECLARE_METATYPE(GrayConversionMode)
Q_DECLARE_METATYPE(Recipe)
