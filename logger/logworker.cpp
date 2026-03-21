#include "logger/logworker.h"

#include <utility>

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QTextStream>

#include "common/constants.h"
#include "logger/logmanager.h"

namespace
{
constexpr int kFlushIntervalMs = 250;
constexpr int kDrainBatchSize = 128;

QString formatLogLine(const LogEvent &event)
{
    return QStringLiteral("[%1] [%2] [%3] %4")
        .arg(event.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
             event.module,
             event.level,
             event.message);
}

LogEvent makeSystemEvent(
    const QString &level,
    const QString &message,
    bool uiVisible)
{
    LogEvent event;
    event.timestamp = QDateTime::currentDateTime();
    event.level = level;
    event.module = QStringLiteral("日志");
    event.message = message;
    event.uiVisible = uiVisible;
    event.persist = true;
    event.threadId = reinterpret_cast<quintptr>(QThread::currentThreadId());
    event.formattedLine = formatLogLine(event);
    return event;
}
}

LogWorker::LogWorker(LogManager *manager, QString logDirectoryPath, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
    , m_logDirectoryPath(std::move(logDirectoryPath))
{
    m_flushTimer.setInterval(kFlushIntervalMs);
    m_flushTimer.setSingleShot(false);
    connect(&m_flushTimer, &QTimer::timeout, this, &LogWorker::flush);
}

void LogWorker::initialize()
{
    m_flushTimer.start();
}

void LogWorker::drainQueue()
{
    if (m_manager == nullptr) {
        return;
    }

    while (true) {
        int droppedLowPriorityCount = 0;
        int droppedHighPriorityCount = 0;
        const QList<LogEvent> batch = m_manager->takePendingEvents(
            kDrainBatchSize,
            &droppedLowPriorityCount,
            &droppedHighPriorityCount);

        if (droppedLowPriorityCount > 0) {
            processEvent(makeSystemEvent(
                QStringLiteral("WARN"),
                QStringLiteral("异步日志队列已满，丢弃 %1 条低优先级诊断日志。")
                    .arg(droppedLowPriorityCount),
                false));
        }

        if (droppedHighPriorityCount > 0) {
            processEvent(makeSystemEvent(
                QStringLiteral("ERROR"),
                QStringLiteral("异步日志队列持续饱和，已覆盖 %1 条高优先级日志，请尽快降低日志量或提高队列容量。")
                    .arg(droppedHighPriorityCount),
                true));
        }

        if (batch.isEmpty()) {
            break;
        }

        for (const LogEvent &event : batch) {
            processEvent(event);
        }
    }
}

void LogWorker::shutdown()
{
    flush();
    if (m_flushTimer.isActive()) {
        m_flushTimer.stop();
    }

    if (m_logFile.isOpen()) {
        m_logFile.close();
    }
}

void LogWorker::processEvent(const LogEvent &event)
{
    if (event.persist) {
        QString fileError;
        if (ensureLogFileReady(event.timestamp.date(), &fileError)) {
            QTextStream stream(&m_logFile);
            stream << event.formattedLine << '\n';
            m_hasPendingFlush = true;
        } else {
            qWarning().noquote()
                << QStringLiteral("[日志] 文件写入不可用：%1").arg(fileError);
        }
    }

    writeToConsole(event);

    if (event.uiVisible) {
        emit uiLogGenerated(event);
    }
}

void LogWorker::flush()
{
    if (!m_hasPendingFlush || !m_logFile.isOpen()) {
        return;
    }

    m_logFile.flush();
    m_hasPendingFlush = false;
}

QString LogWorker::resolvedLogDirectoryPath() const
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

bool LogWorker::ensureLogFileReady(const QDate &date, QString *errorMessage)
{
    if (m_logFile.isOpen() && m_activeLogDate == date) {
        return true;
    }

    if (m_logFile.isOpen()) {
        m_logFile.flush();
        m_logFile.close();
    }

    const QDir logDir(resolvedLogDirectoryPath());
    if (!QDir().mkpath(logDir.absolutePath())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建日志目录：%1").arg(logDir.absolutePath());
        }
        return false;
    }

    const QString fileName =
        logDir.filePath(QStringLiteral("vision_%1.log").arg(date.toString(QStringLiteral("yyyy-MM-dd"))));
    m_logFile.setFileName(fileName);
    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法打开日志文件：%1").arg(fileName);
        }
        return false;
    }

    m_activeLogDate = date;
    return true;
}

void LogWorker::writeToConsole(const LogEvent &event) const
{
    if (event.level == QStringLiteral("ERROR")) {
        qCritical().noquote() << event.formattedLine;
    } else if (event.level == QStringLiteral("DEBUG")) {
        qDebug().noquote() << event.formattedLine;
    } else if (event.level == QStringLiteral("WARN")) {
        qWarning().noquote() << event.formattedLine;
    } else {
        qInfo().noquote() << event.formattedLine;
    }
}
