#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

struct LogEvent
{
    QDateTime timestamp;
    QString level;
    QString module;
    QString message;
    QString formattedLine;
    bool uiVisible = true;
    bool persist = true;
    quintptr threadId = 0;
};

Q_DECLARE_METATYPE(LogEvent)
