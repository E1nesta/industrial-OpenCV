#pragma once

#include <QSqlDatabase>
#include <QString>

class DatabaseManager
{
public:
    explicit DatabaseManager(QString databasePath = QString());

    bool initialize(QString *errorMessage = nullptr) const;
    QString databaseFilePath() const;
    QSqlDatabase openConnection(const QString &connectionName, QString *errorMessage = nullptr) const;
    QString createConnectionName(const QString &suffix) const;

private:
    bool ensureSchema(QSqlDatabase &database, QString *errorMessage = nullptr) const;
    QString resolvedDatabasePath() const;

    QString m_databasePath;
};
