#include "logger/logmanager.h"

#include <QDebug>

#include "common/utils.h"

void LogManager::info(const QString &module, const QString &message) const
{
    write("INFO", module, message);
}

void LogManager::warn(const QString &module, const QString &message) const
{
    write("WARN", module, message);
}

void LogManager::error(const QString &module, const QString &message) const
{
    write("ERROR", module, message);
}

void LogManager::write(const QString &level, const QString &module, const QString &message) const
{
    const QString line = QStringLiteral("[%1] [%2] [%3] %4")
                             .arg(utils::currentTimestamp(), module, level, message);

    if (level == "ERROR") {
        qCritical().noquote() << line;
        return;
    }

    if (level == "WARN") {
        qWarning().noquote() << line;
        return;
    }

    qInfo().noquote() << line;
}

