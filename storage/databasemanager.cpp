#include "storage/databasemanager.h"

#include <utility>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include "common/constants.h"

DatabaseManager::DatabaseManager(QString databasePath)
    : m_databasePath(std::move(databasePath))
{
}

bool DatabaseManager::initialize(QString *errorMessage) const
{
    const QString connectionName = createConnectionName(QStringLiteral("init"));
    bool ok = false;

    {
        QSqlDatabase database = openConnection(connectionName, errorMessage);
        if (database.isOpen()) {
            ok = ensureSchema(database, errorMessage);
            database.close();
        }
    }

    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

QString DatabaseManager::databaseFilePath() const
{
    return resolvedDatabasePath();
}

QSqlDatabase DatabaseManager::openConnection(const QString &connectionName, QString *errorMessage) const
{
    const QString filePath = resolvedDatabasePath();
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    database.setDatabaseName(filePath);

    if (!database.open() && errorMessage != nullptr) {
        *errorMessage = database.lastError().text();
    }

    return database;
}

QString DatabaseManager::createConnectionName(const QString &suffix) const
{
    return QStringLiteral("vision_%1_%2")
        .arg(suffix, QUuid::createUuid().toString(QUuid::WithoutBraces));
}

bool DatabaseManager::ensureSchema(QSqlDatabase &database, QString *errorMessage) const
{
    QSqlQuery query(database);
    const bool ok = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS inspection_records ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "timestamp TEXT NOT NULL,"
        "batch_no TEXT,"
        "is_ok INTEGER NOT NULL,"
        "defect_count INTEGER NOT NULL,"
        "process_time_ms REAL NOT NULL,"
        "image_path TEXT NOT NULL"
        ")"));

    if (!ok && errorMessage != nullptr) {
        *errorMessage = query.lastError().text();
    }

    return ok;
}

QString DatabaseManager::resolvedDatabasePath() const
{
    if (!m_databasePath.isEmpty()) {
        return m_databasePath;
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    return appDir.filePath(
        QStringLiteral("%1/%2")
            .arg(constants::kDataDirectoryName, constants::kDatabaseFileName));
}
