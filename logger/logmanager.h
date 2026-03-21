#pragma once

#include <QObject>
#include <QList>
#include <QMutex>
#include <QQueue>
#include <QThread>
#include <QString>

#include <atomic>

#include "logger/logevent.h"

class LogWorker;

class LogManager : public QObject
{
    Q_OBJECT

public:
    explicit LogManager(QObject *parent = nullptr, QString logDirectoryPath = QString());
    ~LogManager() override;

    void debug(const QString &module, const QString &message, bool uiVisible = false);
    void info(const QString &module, const QString &message, bool uiVisible = true);
    void warn(const QString &module, const QString &message, bool uiVisible = true);
    void error(const QString &module, const QString &message, bool uiVisible = true);
    void setMinimumLevelName(const QString &levelName);

    QString logDirectoryPath() const;
    QString currentLogFilePath() const;
    QString minimumLevelName() const;

signals:
    void uiLogGenerated(const LogEvent &event);
    void drainRequested();
    void minimumLevelChanged(const QString &levelName);

private:
    friend class LogWorker;

    void write(
        const QString &level,
        const QString &module,
        const QString &message,
        bool uiVisible,
        bool persist = true);
    QList<LogEvent> takePendingEvents(
        int maxCount,
        int *droppedLowPriorityCount = nullptr,
        int *droppedHighPriorityCount = nullptr);
    bool enqueueEvent(LogEvent &&event);
    QString resolvedLogDirectoryPath() const;

    QString m_logDirectoryPath;
    std::atomic<int> m_minimumLevelValue{1};
    mutable QMutex m_queueMutex;
    QQueue<LogEvent> m_pendingEvents;
    bool m_drainScheduled = false;
    int m_droppedLowPriorityCount = 0;
    int m_droppedHighPriorityCount = 0;
    QThread m_workerThread;
    LogWorker *m_worker = nullptr;
};
