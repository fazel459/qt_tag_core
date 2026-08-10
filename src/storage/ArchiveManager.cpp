#include "ArchiveManager.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlQuery>
#include <QTextStream>
#include <QSqlError>

ArchiveManager::ArchiveManager(
    DbManager& db,
    const QString& archivePath,
    qint64 maxSizeBytes,
    int checkIntervalMs,
    QObject* parent
)
    : QObject(parent)
    , m_db(db)
    , m_archivePath(archivePath)
    , m_maxSizeBytes(maxSizeBytes)
{
    QDir dir(m_archivePath);

    if (!dir.exists())
    {
        dir.mkpath(".");
    }

    m_checkTimer.setParent(this);
    m_checkTimer.setInterval(checkIntervalMs);

    QObject::connect(&m_checkTimer, &QTimer::timeout, [this]()
    {
        checkAndArchive();
    });

    m_checkTimer.start();

    qInfo() << "ArchiveManager started:"
            << "archivePath=" << m_archivePath
            << "maxSizeBytes=" << m_maxSizeBytes
            << "checkIntervalMs=" << checkIntervalMs;
}

QString ArchiveManager::archivePath() const
{
    return m_archivePath;
}

void ArchiveManager::checkAndArchive()
{
    const qint64 rawSize = getTableSize("tag_values_raw");

    qInfo() << "ArchiveManager: checking table sizes:"
            << "tag_values_raw=" << rawSize << "bytes"
            << "threshold=" << m_maxSizeBytes << "bytes";

    if (rawSize >= m_maxSizeBytes)
    {
        qInfo() << "ArchiveManager: threshold exceeded, starting archive";

        const QDateTime endTime = QDateTime::currentDateTimeUtc().addDays(-1);

        archiveTable("tag_values_raw", endTime);
    }
}

bool ArchiveManager::archiveTable(
    const QString& tableName,
    const QDateTime& endTime
)
{
    const QString timestamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss");
    const QString fileName = QStringLiteral("archive_%1_%2.csv").arg(timestamp).arg(tableName);
    const QString filePath = m_archivePath + "/" + fileName;

    qInfo() << "ArchiveManager: archiving" << tableName
            << "to" << filePath;

    qint64 recordCount = 0;
    const qint64 fileSize = exportToCsv(tableName, endTime, filePath, recordCount);

    if (fileSize < 0)
    {
        qWarning() << "ArchiveManager: archive failed for" << tableName;
        return false;
    }

    logArchive(
        tableName,
        filePath,
        QDateTime(),
        endTime,
        recordCount,
        fileSize
    );

    qInfo() << "ArchiveManager: archive completed:"
            << "table=" << tableName
            << "records=" << recordCount
            << "fileSize=" << fileSize << "bytes";

    return true;
}

qint64 ArchiveManager::getTableSize(const QString& tableName)
{
    QSqlQuery query(m_db.database());

    const QString sql = QStringLiteral(
        "SELECT COALESCE(hypertable_size('%1'), pg_total_relation_size('%1'));"
    ).arg(tableName);

    if (!query.exec(sql))
    {
        qWarning() << "ArchiveManager: failed to get table size:" << query.lastError().text();
        return 0;
    }

    if (query.next())
    {
        return query.value(0).toLongLong();
    }

    return 0;
}

qint64 ArchiveManager::exportToCsv(
    const QString& tableName,
    const QDateTime& endTime,
    const QString& filePath,
    qint64& recordCount
)
{
    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qWarning() << "ArchiveManager: cannot open file for writing:" << filePath;
        return -1;
    }

    QTextStream stream(&file);

    stream << "time,tag_id,raw_value,eng_value,quality,source\n";

    QSqlQuery query(m_db.database());

    const QString sql = QStringLiteral(
        "SELECT time, tag_id, raw_value, eng_value, quality, source "
        "FROM %1 "
        "WHERE time <= :end_time "
        "ORDER BY time ASC;"
    ).arg(tableName);

    query.prepare(sql);
    query.bindValue(":end_time", endTime);

    if (!query.exec())
    {
        qWarning() << "ArchiveManager: query failed:" << query.lastError().text();
        file.close();
        return -1;
    }

    recordCount = 0;

    while (query.next())
    {
        const QString time = query.value("time").toDateTime().toString(Qt::ISODateWithMs);
        const qint64 tagId = query.value("tag_id").toLongLong();
        const double rawValue = query.value("raw_value").toDouble();
        const double engValue = query.value("eng_value").toDouble();
        const int quality = query.value("quality").toInt();
        const QString source = query.value("source").toString();

        stream << time << ","
               << tagId << ","
               << rawValue << ","
               << engValue << ","
               << quality << ","
               << source << "\n";

        ++recordCount;

        if (recordCount % 100000 == 0)
        {
            qInfo() << "ArchiveManager: exported" << recordCount << "records";
        }
    }

    file.close();

    const QFileInfo fileInfo(filePath);

    return fileInfo.size();
}

bool ArchiveManager::importFromCsv(
    const QString& filePath,
    const QString& tableName
)
{
    QFile file(filePath);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qWarning() << "ArchiveManager: cannot open file for reading:" << filePath;
        return false;
    }

    QTextStream stream(&file);

    // رد کردن header
    stream.readLine();

    QSqlQuery query(m_db.database());

    const QString sql = QStringLiteral(
        "INSERT INTO %1 (time, tag_id, raw_value, eng_value, quality, source) "
        "VALUES (:time, :tag_id, :raw_value, :eng_value, :quality, :source);"
    ).arg(tableName);

    query.prepare(sql);

    qint64 importedCount = 0;

    m_db.database().transaction();

    while (!stream.atEnd())
    {
        const QString line = stream.readLine();

        if (line.isEmpty())
        {
            continue;
        }

        const QStringList parts = line.split(',');

        if (parts.size() < 6)
        {
            continue;
        }

        query.bindValue(":time", QDateTime::fromString(parts[0], Qt::ISODateWithMs));
        query.bindValue(":tag_id", parts[1].toLongLong());
        query.bindValue(":raw_value", parts[2].toDouble());
        query.bindValue(":eng_value", parts[3].toDouble());
        query.bindValue(":quality", parts[4].toInt());
        query.bindValue(":source", parts[5]);

        if (!query.exec())
        {
            qWarning() << "ArchiveManager: import failed at record" << importedCount
                       << ":" << query.lastError().text();
        }

        ++importedCount;

        if (importedCount % 10000 == 0)
        {
            m_db.database().commit();
            m_db.database().transaction();

            qInfo() << "ArchiveManager: imported" << importedCount << "records";
        }
    }

    m_db.database().commit();

    file.close();

    qInfo() << "ArchiveManager: import completed:"
            << "records=" << importedCount;

    return true;
}

void ArchiveManager::logArchive(
    const QString& tableName,
    const QString& filePath,
    const QDateTime& startTime,
    const QDateTime& endTime,
    qint64 recordCount,
    qint64 fileSizeBytes
)
{
    QSqlQuery query(m_db.database());

    query.prepare(R"(
        INSERT INTO archive_log (
            table_name,
            file_path,
            start_time,
            end_time,
            record_count,
            file_size_bytes,
            status,
            created_at
        )
        VALUES (
            :table_name,
            :file_path,
            :start_time,
            :end_time,
            :record_count,
            :file_size_bytes,
            'completed',
            now()
        );
    )");

    query.bindValue(":table_name", tableName);
    query.bindValue(":file_path", filePath);
    query.bindValue(":start_time", startTime);
    query.bindValue(":end_time", endTime);
    query.bindValue(":record_count", recordCount);
    query.bindValue(":file_size_bytes", fileSizeBytes);

    if (!query.exec())
    {
        qWarning() << "ArchiveManager: failed to log archive:" << query.lastError().text();
    }
}
