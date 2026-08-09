#include "HistorianManager.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>

HistorianManager::HistorianManager(DbManager& db, QObject* parent)
    : QObject(parent)
    , m_db(db)
{
    qInfo() << "HistorianManager started";
}

QVector<AggregatePoint> HistorianManager::query1Min(
    qint64 tagId,
    const QDateTime& startTime,
    const QDateTime& endTime
)
{
    return queryAggregate("tag_values_1min", tagId, startTime, endTime);
}

QVector<AggregatePoint> HistorianManager::query5Min(
    qint64 tagId,
    const QDateTime& startTime,
    const QDateTime& endTime
)
{
    return queryAggregate("tag_values_5min", tagId, startTime, endTime);
}

QVector<AggregatePoint> HistorianManager::query1Hour(
    qint64 tagId,
    const QDateTime& startTime,
    const QDateTime& endTime
)
{
    return queryAggregate("tag_values_1hour", tagId, startTime, endTime);
}

QVector<AggregatePoint> HistorianManager::query1Day(
    qint64 tagId,
    const QDateTime& startTime,
    const QDateTime& endTime
)
{
    return queryAggregate("tag_values_1day", tagId, startTime, endTime);
}

QVector<AggregatePoint> HistorianManager::queryAggregate(
    const QString& viewName,
    qint64 tagId,
    const QDateTime& startTime,
    const QDateTime& endTime
)
{
    QVector<AggregatePoint> results;

    QSqlQuery query(m_db.database());

    const QString sql = QStringLiteral(
        "SELECT bucket, tag_id, avg_value, min_value, max_value, first_value, last_value, sample_count "
        "FROM %1 "
        "WHERE tag_id = :tag_id "
        "AND bucket >= :start_time "
        "AND bucket <= :end_time "
        "ORDER BY bucket ASC;"
    ).arg(viewName);

    query.prepare(sql);

    query.bindValue(":tag_id", tagId);
    query.bindValue(":start_time", startTime);
    query.bindValue(":end_time", endTime);

    if (!query.exec())
    {
        qWarning() << "HistorianManager: query failed:" << query.lastError().text();
        return results;
    }

    while (query.next())
    {
        AggregatePoint point;

        point.bucket = query.value("bucket").toDateTime();
        point.tagId = query.value("tag_id").toLongLong();
        point.avgValue = query.value("avg_value").toDouble();
        point.minValue = query.value("min_value").toDouble();
        point.maxValue = query.value("max_value").toDouble();
        point.firstValue = query.value("first_value").toDouble();
        point.lastValue = query.value("last_value").toDouble();
        point.sampleCount = query.value("sample_count").toInt();

        results.push_back(point);
    }

    return results;
}

void HistorianManager::refreshContinuousAggregates()
{
    const QStringList views = {
        "tag_values_1min",
        "tag_values_5min",
        "tag_values_1hour",
        "tag_values_1day"
    };

    for (const QString& view : views)
    {
        refreshContinuousAggregate(view);
    }
}

void HistorianManager::refreshContinuousAggregate(const QString& viewName)
{
    QSqlQuery query(m_db.database());

    const QString sql = QStringLiteral(
        "CALL refresh_continuous_aggregate('%1', NULL, NULL);"
    ).arg(viewName);

    if (!query.exec(sql))
    {
        qWarning() << "HistorianManager: refresh failed for" << viewName
                   << ":" << query.lastError().text();
    }
    else
    {
        qInfo() << "HistorianManager: refreshed" << viewName;
    }
}

void HistorianManager::refreshContinuousAggregate(
    const QString& viewName,
    const QDateTime& startTime,
    const QDateTime& endTime
)
{
    QSqlQuery query(m_db.database());

    const QString sql = QStringLiteral(
        "CALL refresh_continuous_aggregate('%1', :start_time, :end_time);"
    ).arg(viewName);

    query.prepare(sql);

    query.bindValue(":start_time", startTime);
    query.bindValue(":end_time", endTime);

    if (!query.exec())
    {
        qWarning() << "HistorianManager: refresh failed for" << viewName
                   << ":" << query.lastError().text();
    }
    else
    {
        qInfo() << "HistorianManager: refreshed" << viewName
                << "from" << startTime.toString(Qt::ISODate)
                << "to" << endTime.toString(Qt::ISODate);
    }
}
