#pragma once

#include <atomic>

#include <QObject>

#include "models/detectionoutput.h"

class LogManager;

class DetectionWorker : public QObject
{
    Q_OBJECT

public:
    explicit DetectionWorker(LogManager *logManager, QObject *parent = nullptr);

    void resetCancellation();
    void requestCancel();

public slots:
    void process(const DetectionRequest &request);

signals:
    void completed(const DetectionOutput &output);
    void failed(const QString &inspectionId, const QString &errorMessage);
    void canceled(const QString &inspectionId);

private:
    LogManager *m_logManager = nullptr;
    std::atomic_bool m_cancelRequested = false;
};
