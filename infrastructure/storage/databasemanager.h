// 基础设施存储：databasemanager.h 负责记录落库与图片归档。
// 本文件承接巡检结果留痕出口，处理持久化副作用。
#pragma once

#include <QSqlDatabase>
#include <QString>

// DatabaseManager 负责 SQLite 连接创建与表结构初始化。
// 该类只提供底层数据库能力，不包含业务记录读写逻辑。
class DatabaseManager
{
public:
    explicit DatabaseManager(QString databasePath = QString());

    // 数据库初始化与连接工厂。
    bool initialize(QString *errorMessage = nullptr) const;
    QString databaseFilePath() const;
    QSqlDatabase openConnection(const QString &connectionName, QString *errorMessage = nullptr) const;
    QString createConnectionName(const QString &suffix) const;

private:
    // 内部 schema 保障逻辑。
    bool ensureSchema(QSqlDatabase &database, QString *errorMessage = nullptr) const;
    bool ensureColumnExists(
        QSqlDatabase &database,
        const QString &columnName,
        const QString &columnDefinition,
        QString *errorMessage = nullptr) const;
    QString resolvedDatabasePath() const;

    QString m_databasePath;
};
