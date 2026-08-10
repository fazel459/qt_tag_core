#ifndef ARCHIVEMANAGER_H
#define ARCHIVEMANAGER_H
#pragma once

#include <QDateTime>
#include <QObject>
#include <QTimer>

#include "../core/Models.h"
#include "DbManager.h"

class ArchiveManager : public QObject
{
public:
    ArchiveManager(
        DbManager& db,
        const QString& archivePath,
        qint64 maxSizeBytes,
        int checkIntervalMs,
        QObject* parent = nullptr
    );

    void checkAndArchive();

    bool archiveTable(
        const QString& tableName,
        const QDateTime& endTime
    );

    bool restoreArchive(const QString& filePath, const QString& tableName);

    QString archivePath() const;

private:
    qint64 getTableSize(const QString& tableName);

    qint64 exportToCsv(
        const QString& tableName,
        const QDateTime& endTime,
        const QString& filePath,
        qint64& recordCount
    );

    bool importFromCsv(
        const QString& filePath,
        const QString& tableName
    );

    void logArchive(
        const QString& tableName,
        const QString& filePath,
        const QDateTime& startTime,
        const QDateTime& endTime,
        qint64 recordCount,
        qint64 fileSizeBytes
    );

    DbManager& m_db;

    QString m_archivePath;
    qint64 m_maxSizeBytes;

    QTimer m_checkTimer;
};
#endif // ARCHIVEMANAGER_H
