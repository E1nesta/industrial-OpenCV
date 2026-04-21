// 基础设施存储：databasemanager.cpp 负责记录落库与图片归档。
// 本文件承接巡检结果留痕出口，处理持久化副作用。
#include "infrastructure/storage/databasemanager.h"

#include <utility>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include "common/config/constants.h"

DatabaseManager::DatabaseManager(QString databasePath)
    : m_databasePath(std::move(databasePath))
{
}

bool DatabaseManager::initialize(QString *errorMessage) const
{
    // 初始化阶段使用独立短生命周期连接，避免污染业务读写连接。
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
    // 统一确保数据库目录存在，避免首次运行时路径不可写导致打开失败。
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
    // 记录表采用轻量单表模型，字段升级通过按列补齐保证向后兼容。
    QSqlQuery query(database);
    const bool tableOk = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS inspection_records ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "inspection_id TEXT,"
        "capture_id TEXT,"
        "timestamp TEXT NOT NULL,"
        "batch_no TEXT,"
        "recipe_name TEXT,"
        "source_type TEXT,"
        "source_path TEXT,"
        "source_name TEXT,"
        "frame_index INTEGER,"
        "is_ok INTEGER NOT NULL,"
        "defect_count INTEGER NOT NULL,"
        "process_time_ms REAL NOT NULL,"
        "summary_text TEXT,"
        "image_path TEXT,"
        "result_image_path TEXT"
        ")"));

    if (!tableOk && errorMessage != nullptr) {
        *errorMessage = query.lastError().text();
    }

    if (!tableOk) {
        return false;
    }

    if (!ensureColumnExists(
            database,
            QStringLiteral("capture_id"),
            QStringLiteral("TEXT"),
            errorMessage)) {
        return false;
    }

    if (!ensureColumnExists(
            database,
            QStringLiteral("recipe_name"),
            QStringLiteral("TEXT"),
            errorMessage)) {
        return false;
    }

    if (!ensureColumnExists(
            database,
            QStringLiteral("source_type"),
            QStringLiteral("TEXT"),
            errorMessage)) {
        return false;
    }

    if (!ensureColumnExists(
            database,
            QStringLiteral("source_path"),
            QStringLiteral("TEXT"),
            errorMessage)) {
        return false;
    }

    if (!ensureColumnExists(
            database,
            QStringLiteral("source_name"),
            QStringLiteral("TEXT"),
            errorMessage)) {
        return false;
    }

    if (!ensureColumnExists(
            database,
            QStringLiteral("frame_index"),
            QStringLiteral("INTEGER"),
            errorMessage)) {
        return false;
    }

    const bool resultImageColumnOk = ensureColumnExists(
        database,
        QStringLiteral("result_image_path"),
        QStringLiteral("TEXT"),
        errorMessage);
    if (!resultImageColumnOk) {
        return false;
    }

    if (!ensureColumnExists(
            database,
            QStringLiteral("summary_text"),
            QStringLiteral("TEXT"),
            errorMessage)) {
        return false;
    }

    return ensureColumnExists(
        database,
        QStringLiteral("inspection_id"),
        QStringLiteral("TEXT"),
        errorMessage);
}

bool DatabaseManager::ensureColumnExists(
    QSqlDatabase &database,
    const QString &columnName,
    const QString &columnDefinition,
    QString *errorMessage) const
{
    // 先查询现有表结构，列存在则直接复用；不存在再增量补列。
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA table_info(inspection_records)"))) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    while (query.next()) {
        if (query.value(1).toString() == columnName) {
            return true;
        }
    }

    QSqlQuery alterQuery(database);
    const bool ok = alterQuery.exec(
        QStringLiteral("ALTER TABLE inspection_records ADD COLUMN %1 %2")
            .arg(columnName, columnDefinition));
    if (!ok && errorMessage != nullptr) {
        *errorMessage = alterQuery.lastError().text();
    }

    return ok;
}

QString DatabaseManager::resolvedDatabasePath() const
{
    if (!m_databasePath.isEmpty()) {
        return m_databasePath;
    }

    // 未显式配置时，默认落在程序目录下 data/inspection.db。
    const QDir appDir(QCoreApplication::applicationDirPath());
    return appDir.filePath(
        QStringLiteral("%1/%2")
            .arg(constants::kDataDirectoryName, constants::kDatabaseFileName));
}
