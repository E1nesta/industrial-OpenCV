// 日志实体：LogEvent 定义单条日志事件结构。
// 该对象用于日志线程、持久化和 UI 日志面板之间传递。
#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

struct LogEvent
{
    // 日志时间戳。
    QDateTime timestamp;
    // 日志级别文本。
    QString level;
    // 日志模块名。
    QString module;
    // 原始日志消息。
    QString message;
    // 格式化后的显示文本。
    QString formattedLine;
    // 是否显示在 UI。
    bool uiVisible = true;
    // 是否允许持久化。
    bool persist = true;
    // 产生日志的线程 ID。
    quintptr threadId = 0;
};

Q_DECLARE_METATYPE(LogEvent)
