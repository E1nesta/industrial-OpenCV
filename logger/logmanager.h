#pragma once

#include <QString>

class LogManager
{
public:
    void info(const QString &module, const QString &message) const;
    void warn(const QString &module, const QString &message) const;
    void error(const QString &module, const QString &message) const;

private:
    void write(const QString &level, const QString &module, const QString &message) const;
};

