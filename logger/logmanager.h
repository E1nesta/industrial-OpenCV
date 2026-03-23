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

// LogManager 提供线程安全的日志入口，负责日志排队与 worker 分发。
// 业务层只调用 debug/info/warn/error，不直接处理文件与刷盘细节。
class LogManager : public QObject
{
    Q_OBJECT

public:
    explicit LogManager(QObject *parent = nullptr, QString logDirectoryPath = QString());
    ~LogManager() override;

    // 对外日志写入接口。
    void debug(const QString &module, const QString &message, bool uiVisible = false);
    void info(const QString &module, const QString &message, bool uiVisible = true);
    void warn(const QString &module, const QString &message, bool uiVisible = true);
    void error(const QString &module, const QString &message, bool uiVisible = true);
    void setMinimumLevelName(const QString &levelName);

    // 运行时查询接口。
    QString logDirectoryPath() const;
    QString currentLogFilePath() const;
    QString minimumLevelName() const;

signals:
    // 发给 UI 和日志 worker 的异步回调信号。
    void uiLogGenerated(const LogEvent &event);
    void drainRequested();
    void minimumLevelChanged(const QString &levelName);

private:
    friend class LogWorker;

    // 队列写入与队列读取辅助。
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

    // 配置与队列状态。
    QString m_logDirectoryPath;
    std::atomic<int> m_minimumLevelValue{1};
    mutable QMutex m_queueMutex;
    QQueue<LogEvent> m_pendingEvents;
    bool m_drainScheduled = false;
    int m_droppedLowPriorityCount = 0;
    int m_droppedHighPriorityCount = 0;

    // 后台日志线程与 worker。
    QThread m_workerThread;
    LogWorker *m_worker = nullptr;
};
