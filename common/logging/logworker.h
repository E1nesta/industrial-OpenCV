#pragma once

#include <QDate>
#include <QFile>
#include <QObject>
#include <QString>
#include <QTimer>

#include "common/logging/logevent.h"

class LogManager;

// LogWorker 在后台线程消费日志队列，负责文件写入、控制台输出与 UI 分发。
class LogWorker : public QObject
{
    Q_OBJECT

public:
    explicit LogWorker(LogManager *manager, QString logDirectoryPath, QObject *parent = nullptr);

signals:
    // 发给 UI 的可见日志事件。
    void uiLogGenerated(const LogEvent &event);

public slots:
    // 线程生命周期入口：初始化、消费队列、关闭收尾。
    void initialize();
    void drainQueue();
    void shutdown();

private slots:
    // 定时刷盘槽：把缓冲日志批量落盘，降低频繁 I/O 开销。
    void flush();

private:
    // 队列消费与文件写入辅助。
    void processEvent(const LogEvent &event);
    QString resolvedLogDirectoryPath() const;
    bool ensureLogFileReady(const QDate &date, QString *errorMessage = nullptr);
    void writeToConsole(const LogEvent &event) const;

    // 上下文与文件状态。
    LogManager *m_manager = nullptr;
    QString m_logDirectoryPath;
    QFile m_logFile;
    QDate m_activeLogDate;

    // 定时刷盘状态。
    QTimer *m_flushTimer = nullptr;
    bool m_hasPendingFlush = false;
};
