#include "common/logging/logmanager.h"

#include <algorithm>
#include <utility>

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QMetaObject>
#include <QThread>

#include "common/config/constants.h"
#include "common/logging/logworker.h"

namespace
{
constexpr int kMaxPendingLogEvents = 1024;
constexpr int kDebugLevelValue = 0;
constexpr int kInfoLevelValue = 1;
constexpr int kWarnLevelValue = 2;
constexpr int kErrorLevelValue = 3;

int levelValue(const QString &levelName)
{
    const QString normalized = levelName.trimmed().toUpper();
    if (normalized == QStringLiteral("DEBUG")) {
        return kDebugLevelValue;
    }
    if (normalized == QStringLiteral("WARN")) {
        return kWarnLevelValue;
    }
    if (normalized == QStringLiteral("ERROR")) {
        return kErrorLevelValue;
    }
    return kInfoLevelValue;
}

QString normalizedLevelName(const QString &levelName)
{
    const int value = levelValue(levelName);
    if (value == kDebugLevelValue) {
        return QStringLiteral("DEBUG");
    }
    if (value == kWarnLevelValue) {
        return QStringLiteral("WARN");
    }
    if (value == kErrorLevelValue) {
        return QStringLiteral("ERROR");
    }
    return QStringLiteral("INFO");
}

QString levelNameFromValue(int value)
{
    if (value == kDebugLevelValue) {
        return QStringLiteral("DEBUG");
    }
    if (value == kWarnLevelValue) {
        return QStringLiteral("WARN");
    }
    if (value == kErrorLevelValue) {
        return QStringLiteral("ERROR");
    }
    return QStringLiteral("INFO");
}

QString formatLogLine(const LogEvent &event)
{
    return QStringLiteral("[%1] [%2] [%3] %4")
        .arg(event.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
             event.module,
             event.level,
             event.message);
}

bool isLowPriorityEvent(const LogEvent &event)
{
    return event.level == QStringLiteral("DEBUG")
           || (event.level == QStringLiteral("INFO") && !event.uiVisible);
}
} // namespace

LogManager::LogManager(QObject *parent, QString logDirectoryPath)
    : QObject(parent)
    , m_logDirectoryPath(std::move(logDirectoryPath))
    , m_worker(new LogWorker(this, m_logDirectoryPath))
{
    // 日志事件跨线程传输前先注册元类型。
    qRegisterMetaType<LogEvent>("LogEvent");

    m_worker->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::started, m_worker, &LogWorker::initialize);
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &LogManager::drainRequested, m_worker, &LogWorker::drainQueue, Qt::QueuedConnection);
    connect(m_worker, &LogWorker::uiLogGenerated, this, &LogManager::uiLogGenerated, Qt::QueuedConnection);
    // LogManager 只负责入队和分发，实际写盘在 worker 线程执行。
    m_workerThread.start();
}

LogManager::~LogManager()
{
    if (m_worker != nullptr && m_workerThread.isRunning()) {
        QMetaObject::invokeMethod(m_worker, "drainQueue", Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(m_worker, "shutdown", Qt::BlockingQueuedConnection);
        m_workerThread.quit();
        m_workerThread.wait();
    }
}

void LogManager::debug(const QString &module, const QString &message, bool uiVisible)
{
    // 对外日志入口：DEBUG 级别。
    write(QStringLiteral("DEBUG"), module, message, uiVisible);
}

void LogManager::info(const QString &module, const QString &message, bool uiVisible)
{
    // 对外日志入口：INFO 级别。
    write(QStringLiteral("INFO"), module, message, uiVisible);
}

void LogManager::warn(const QString &module, const QString &message, bool uiVisible)
{
    // 对外日志入口：WARN 级别。
    write(QStringLiteral("WARN"), module, message, uiVisible);
}

void LogManager::error(const QString &module, const QString &message, bool uiVisible)
{
    // 对外日志入口：ERROR 级别。
    write(QStringLiteral("ERROR"), module, message, uiVisible);
}

void LogManager::setMinimumLevelName(const QString &levelName)
{
    // 仅在级别发生变化时广播，避免无意义 UI 刷新。
    const QString normalized = normalizedLevelName(levelName);
    const int newValue = levelValue(normalized);
    const int oldValue = m_minimumLevelValue.exchange(newValue);
    if (oldValue == newValue) {
        return;
    }

    emit minimumLevelChanged(normalized);
}

QString LogManager::logDirectoryPath() const
{
    return resolvedLogDirectoryPath();
}

QString LogManager::currentLogFilePath() const
{
    return resolvedLogDirectoryPath() + QStringLiteral("/vision_%1.log")
        .arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
}

QString LogManager::minimumLevelName() const
{
    return levelNameFromValue(m_minimumLevelValue.load());
}

void LogManager::write(
    const QString &level,
    const QString &module,
    const QString &message,
    bool uiVisible,
    bool persist)
{
    // 先按最小级别过滤，避免低优先级日志进入队列。
    const QString normalizedLevel = normalizedLevelName(level);
    if (levelValue(normalizedLevel) < m_minimumLevelValue.load()) {
        return;
    }

    LogEvent event;
    event.timestamp = QDateTime::currentDateTime();
    event.level = normalizedLevel;
    event.module = module;
    event.message = message;
    event.uiVisible = uiVisible;
    event.persist = persist;
    event.threadId = reinterpret_cast<quintptr>(QThread::currentThreadId());
    event.formattedLine = formatLogLine(event);
    if (enqueueEvent(std::move(event))) {
        // 只在“首次从空队列变为非空”时触发一次 drain 请求。
        emit drainRequested();
    }
}

QList<LogEvent> LogManager::takePendingEvents(
    int maxCount,
    int *droppedLowPriorityCount,
    int *droppedHighPriorityCount)
{
    // 批量出队入口：由 worker 线程周期性调用。
    QList<LogEvent> batch;
    QMutexLocker locker(&m_queueMutex);

    const qsizetype takeCount = std::min<qsizetype>(maxCount, m_pendingEvents.size());
    batch.reserve(takeCount);
    for (qsizetype index = 0; index < takeCount; ++index) {
        batch.append(m_pendingEvents.dequeue());
    }

    if (droppedLowPriorityCount != nullptr) {
        *droppedLowPriorityCount = m_droppedLowPriorityCount;
    }
    if (droppedHighPriorityCount != nullptr) {
        *droppedHighPriorityCount = m_droppedHighPriorityCount;
    }
    m_droppedLowPriorityCount = 0;
    m_droppedHighPriorityCount = 0;

    if (m_pendingEvents.isEmpty()) {
        m_drainScheduled = false;
    }

    return batch;
}

bool LogManager::enqueueEvent(LogEvent &&event)
{
    // 入队入口：队列满时按优先级执行丢弃策略。
    QMutexLocker locker(&m_queueMutex);

    // 队列满时优先丢低优先级日志，尽量保留高优先级事件。
    if (m_pendingEvents.size() >= kMaxPendingLogEvents) {
        int removableIndex = -1;
        for (int index = 0; index < m_pendingEvents.size(); ++index) {
            if (isLowPriorityEvent(m_pendingEvents.at(index))) {
                removableIndex = index;
                break;
            }
        }

        if (removableIndex >= 0) {
            m_pendingEvents.removeAt(removableIndex);
            ++m_droppedLowPriorityCount;
        } else if (isLowPriorityEvent(event)) {
            ++m_droppedLowPriorityCount;
            return false;
        } else {
            m_pendingEvents.dequeue();
            ++m_droppedHighPriorityCount;
        }
    }

    m_pendingEvents.enqueue(std::move(event));
    if (m_drainScheduled) {
        return false;
    }

    m_drainScheduled = true;
    return true;
}

QString LogManager::resolvedLogDirectoryPath() const
{
    if (!m_logDirectoryPath.trimmed().isEmpty()) {
        const QFileInfo pathInfo(m_logDirectoryPath);
        if (pathInfo.isAbsolute()) {
            return pathInfo.absoluteFilePath();
        }
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString relativePath = m_logDirectoryPath.trimmed().isEmpty()
                                     ? QString::fromUtf8(constants::kLogDirectoryName)
                                     : m_logDirectoryPath.trimmed();
    return appDir.filePath(relativePath);
}
