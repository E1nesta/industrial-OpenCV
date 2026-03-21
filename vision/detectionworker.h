#pragma once

#include <atomic>

#include <QImage>
#include <QObject>

#include "models/detectresult.h"
#include "models/visionparam.h"

class DetectionWorker : public QObject
{
    Q_OBJECT

public:
    explicit DetectionWorker(QObject *parent = nullptr);

    void resetCancellation();
    void requestCancel();

public slots:
    void process(const QString &imagePath, const VisionParam &param);

signals:
    void completed(const DetectResult &result, const QImage &resultImage);
    void failed(const QString &errorMessage);
    void canceled();

private:
    std::atomic_bool m_cancelRequested = false;
};
