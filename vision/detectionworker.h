#pragma once

#include <atomic>

#include <QImage>
#include <QObject>

#include "models/detectresult.h"
#include "models/visionparam.h"

class LogManager;

class DetectionWorker : public QObject
{
    Q_OBJECT

public:
    explicit DetectionWorker(LogManager *logManager, QObject *parent = nullptr);

    void resetCancellation();
    void requestCancel();

public slots:
    void process(const QString &inspectionId, const QString &imagePath, const VisionParam &param);

signals:
    void completed(const DetectResult &result, const QImage &resultImage);
    void failed(const QString &errorMessage);
    void canceled();

private:
    LogManager *m_logManager = nullptr;
    std::atomic_bool m_cancelRequested = false;
};
