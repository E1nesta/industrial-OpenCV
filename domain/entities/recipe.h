#pragma once

#include <QMetaType>
#include <QRect>
#include <QString>

enum class GrayConversionMode
{
    StableManual,
    OpenCvCvtColor
};

inline QString grayConversionModeToString(GrayConversionMode mode)
{
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
    if (value.compare(QStringLiteral("opencv_cvtcolor"), Qt::CaseInsensitive) == 0) {
        return GrayConversionMode::OpenCvCvtColor;
    }

    return GrayConversionMode::StableManual;
}

struct Recipe
{
    int threshold = 128;
    int minArea = 10;
    int maxArea = 1000;
    QRect roi;
    bool enableMorphology = false;
    GrayConversionMode grayConversionMode = GrayConversionMode::StableManual;
    QString imageSavePath = QStringLiteral("data/images");
};

Q_DECLARE_METATYPE(GrayConversionMode)
Q_DECLARE_METATYPE(Recipe)
