#pragma once

#include <QDate>
#include <QFile>
#include <QObject>
#include <QString>
#include <QTimer>

#include "logger/logevent.h"

class LogManager;

class LogWorker : public QObject
{
    Q_OBJECT

public:
    explicit LogWorker(LogManager *manager, QString logDirectoryPath, QObject *parent = nullptr);

public slots:
    void initialize();
    void drainQueue();
    void shutdown();

signals:
    void uiLogGenerated(const LogEvent &event);

private slots:
    void flush();

private:
    void processEvent(const LogEvent &event);
    QString resolvedLogDirectoryPath() const;
    bool ensureLogFileReady(const QDate &date, QString *errorMessage = nullptr);
    void writeToConsole(const LogEvent &event) const;

    LogManager *m_manager = nullptr;
    QString m_logDirectoryPath;
    QFile m_logFile;
    QDate m_activeLogDate;
    QTimer m_flushTimer;
    bool m_hasPendingFlush = false;
};
