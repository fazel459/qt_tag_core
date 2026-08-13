#include "ReportGenerator.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>

ReportGenerator::ReportGenerator(DbManager& db, QObject* parent)
    : QObject(parent)
    , m_db(db)
{
}

ReportGenerator::~ReportGenerator()
{
}

// ============================================================
// Tag History Report
// ============================================================

QVector<QJsonObject> ReportGenerator::queryTagHistory(const TagHistoryQuery& query)
{
    QVector<QJsonObject> results;

    if (query.tagIds.isEmpty()) {
        return results;
    }

    QSqlQuery sqlQuery(m_db.database());

    // ساخت placeholder برای IN clause
    QStringList placeholders;
    for (int i = 0; i < query.tagIds.size(); ++i) {
        placeholders << QString(":tag_%1").arg(i);
    }
    const QString inClause = placeholders.join(",");

    if (query.interval == "raw") {
        // Raw data query
        const QString sql = QStringLiteral(
            "SELECT time, tag_id, raw_value, eng_value, quality, source "
            "FROM tag_values_raw "
            "WHERE tag_id IN (%1) "
            "AND time >= :from_time AND time < :to_time "
            "ORDER BY time DESC "
            "LIMIT :limit"
        ).arg(inClause);

        sqlQuery.prepare(sql);

        for (int i = 0; i < query.tagIds.size(); ++i) {
            sqlQuery.bindValue(QString(":tag_%1").arg(i), query.tagIds[i]);
        }
        sqlQuery.bindValue(":from_time", query.from);
        sqlQuery.bindValue(":to_time", query.to);
        sqlQuery.bindValue(":limit", query.limit);

        if (!sqlQuery.exec()) {
            qWarning() << "Tag history query failed:" << sqlQuery.lastError().text();
            return results;
        }

        while (sqlQuery.next()) {
            QJsonObject obj;
            obj.insert("time", sqlQuery.value(0).toDateTime().toString(Qt::ISODateWithMs));
            obj.insert("tag_id", sqlQuery.value(1).toLongLong());
            obj.insert("raw_value", sqlQuery.value(2).toDouble());
            obj.insert("eng_value", sqlQuery.value(3).toDouble());
            obj.insert("quality", sqlQuery.value(4).toInt());
            obj.insert("source", sqlQuery.value(5).toString());
            results.append(obj);
        }
    } else {
        // Aggregated query با time_bucket
        QString intervalStr = query.interval;

        // اعتبارسنجی interval
        QStringList validIntervals = {"1 second", "1 minute", "5 minutes", "1 hour", "1 day"};
        if (!validIntervals.contains(intervalStr)) {
            intervalStr = "1 minute";
        }

        const QString sql = QStringLiteral(
            "SELECT "
            "  time_bucket(INTERVAL '%1', time) AS bucket, "
            "  tag_id, "
            "  avg(eng_value) AS avg_value, "
            "  min(eng_value) AS min_value, "
            "  max(eng_value) AS max_value, "
            "  first(eng_value, time) AS first_value, "
            "  last(eng_value, time) AS last_value, "
            "  count(*) AS sample_count "
            "FROM tag_values_raw "
            "WHERE tag_id IN (%2) "
            "AND time >= :from_time AND time < :to_time "
            "GROUP BY bucket, tag_id "
            "ORDER BY bucket ASC "
            "LIMIT :limit"
        ).arg(intervalStr, inClause);

        sqlQuery.prepare(sql);

        for (int i = 0; i < query.tagIds.size(); ++i) {
            sqlQuery.bindValue(QString(":tag_%1").arg(i), query.tagIds[i]);
        }
        sqlQuery.bindValue(":from_time", query.from);
        sqlQuery.bindValue(":to_time", query.to);
        sqlQuery.bindValue(":limit", query.limit);

        if (!sqlQuery.exec()) {
            qWarning() << "Tag history aggregate query failed:" << sqlQuery.lastError().text();
            return results;
        }

        while (sqlQuery.next()) {
            QJsonObject obj;
            obj.insert("bucket", sqlQuery.value(0).toDateTime().toString(Qt::ISODateWithMs));
            obj.insert("tag_id", sqlQuery.value(1).toLongLong());
            obj.insert("avg", sqlQuery.value(2).toDouble());
            obj.insert("min", sqlQuery.value(3).toDouble());
            obj.insert("max", sqlQuery.value(4).toDouble());
            obj.insert("first", sqlQuery.value(5).toDouble());
            obj.insert("last", sqlQuery.value(6).toDouble());
            obj.insert("count", sqlQuery.value(7).toInt());
            results.append(obj);
        }
    }

    return results;
}

QByteArray ReportGenerator::generateTagHistoryReport(const TagHistoryQuery& query, const QString& format)
{
    const QVector<QJsonObject> data = queryTagHistory(query);

    if (format == "csv") {
        QStringList columns;
        if (query.interval == "raw") {
         QStringList   columns = {"time", "tag_id", "raw_value", "eng_value", "quality", "source"};
         return toCsv(data, columns);
        } else {
            QStringList columns = {"bucket", "tag_id", "avg", "min", "max", "first", "last", "count"};
            return toCsv(data, columns);
        }

    }

    return toJson(data);
}

// ============================================================
// Alarm Report
// ============================================================

QVector<QJsonObject> ReportGenerator::queryAlarms(const AlarmReportQuery& query)
{
    QVector<QJsonObject> results;

    QSqlQuery sqlQuery(m_db.database());

    QString whereClause = "WHERE active_time >= :from_time AND active_time < :to_time";

    if (!query.severity.isEmpty()) {
        whereClause += " AND severity = :severity";
    }

    if (!query.state.isEmpty()) {
        whereClause += " AND state = :state";
    }

    if (query.tagId > 0) {
        whereClause += " AND tag_id = :tag_id";
    }

    const QString sql = QStringLiteral(
        "SELECT alarm_id, tag_id, alarm_type, severity, state, value, threshold, "
        "message, active_time, clear_time, ack_time, ack_user "
        "FROM alarms "
        "%1 "
        "ORDER BY active_time DESC "
        "LIMIT :limit"
    ).arg(whereClause);

    sqlQuery.prepare(sql);
    sqlQuery.bindValue(":from_time", query.from);
    sqlQuery.bindValue(":to_time", query.to);

    if (!query.severity.isEmpty()) {
        sqlQuery.bindValue(":severity", query.severity);
    }
    if (!query.state.isEmpty()) {
        sqlQuery.bindValue(":state", query.state);
    }
    if (query.tagId > 0) {
        sqlQuery.bindValue(":tag_id", query.tagId);
    }

    sqlQuery.bindValue(":limit", query.limit);

    if (!sqlQuery.exec()) {
        qWarning() << "Alarm report query failed:" << sqlQuery.lastError().text();
        return results;
    }

    while (sqlQuery.next()) {
        QJsonObject obj;
        obj.insert("alarm_id", sqlQuery.value(0).toLongLong());
        obj.insert("tag_id", sqlQuery.value(1).toLongLong());
        obj.insert("alarm_type", sqlQuery.value(2).toString());
        obj.insert("severity", sqlQuery.value(3).toString());
        obj.insert("state", sqlQuery.value(4).toString());
        obj.insert("value", sqlQuery.value(5).toDouble());
        obj.insert("threshold", sqlQuery.value(6).toDouble());
        obj.insert("message", sqlQuery.value(7).toString());
        obj.insert("active_time", sqlQuery.value(8).toDateTime().toString(Qt::ISODateWithMs));

        if (!sqlQuery.value(9).isNull()) {
            obj.insert("clear_time", sqlQuery.value(9).toDateTime().toString(Qt::ISODateWithMs));
        }
        if (!sqlQuery.value(10).isNull()) {
            obj.insert("ack_time", sqlQuery.value(10).toDateTime().toString(Qt::ISODateWithMs));
        }
        if (!sqlQuery.value(11).isNull()) {
            obj.insert("ack_user", sqlQuery.value(11).toString());
        }

        results.append(obj);
    }

    return results;
}

QByteArray ReportGenerator::generateAlarmReport(const AlarmReportQuery& query, const QString& format)
{
    const QVector<QJsonObject> data = queryAlarms(query);

    if (format == "csv") {
        QStringList columns = {"alarm_id", "tag_id", "alarm_type", "severity", "state",
                               "value", "threshold", "message", "active_time",
                               "clear_time", "ack_time", "ack_user"};
        return toCsv(data, columns);
    }

    return toJson(data);
}

// ============================================================
// Daily Summary Report
// ============================================================

QVector<QJsonObject> ReportGenerator::queryDailySummary(const DailySummaryQuery& query)
{
    QVector<QJsonObject> results;

    if (query.tagIds.isEmpty()) {
        return results;
    }

    // ساخت open/close time برای روز
    QDateTime dayStart = QDateTime(query.date, QTime(0, 0, 0), Qt::UTC);
    QDateTime dayEnd = dayStart.addDays(1);

    QSqlQuery sqlQuery(m_db.database());

    // ساخت placeholder برای IN clause
    QStringList placeholders;
    for (int i = 0; i < query.tagIds.size(); ++i) {
        placeholders << QString(":tag_%1").arg(i);
    }
    const QString inClause = placeholders.join(",");

    const QString sql = QStringLiteral(
        "SELECT "
        "  tag_id, "
        "  avg(eng_value) AS avg_value, "
        "  min(eng_value) AS min_value, "
        "  max(eng_value) AS max_value, "
        "  first(eng_value, time) AS first_value, "
        "  last(eng_value, time) AS last_value, "
        "  count(*) AS sample_count "
        "FROM tag_values_raw "
        "WHERE tag_id IN (%1) "
        "AND time >= :from_time AND time < :to_time "
        "GROUP BY tag_id "
        "ORDER BY tag_id ASC"
    ).arg(inClause);

    sqlQuery.prepare(sql);

    for (int i = 0; i < query.tagIds.size(); ++i) {
        sqlQuery.bindValue(QString(":tag_%1").arg(i), query.tagIds[i]);
    }
    sqlQuery.bindValue(":from_time", dayStart);
    sqlQuery.bindValue(":to_time", dayEnd);

    if (!sqlQuery.exec()) {
        qWarning() << "Daily summary query failed:" << sqlQuery.lastError().text();
        return results;
    }

    while (sqlQuery.next()) {
        QJsonObject obj;
        obj.insert("date", query.date.toString(Qt::ISODate));
        obj.insert("tag_id", sqlQuery.value(0).toLongLong());
        obj.insert("avg", sqlQuery.value(1).toDouble());
        obj.insert("min", sqlQuery.value(2).toDouble());
        obj.insert("max", sqlQuery.value(3).toDouble());
        obj.insert("first", sqlQuery.value(4).toDouble());
        obj.insert("last", sqlQuery.value(5).toDouble());
        obj.insert("count", sqlQuery.value(6).toInt());
        results.append(obj);
    }

    return results;
}

QByteArray ReportGenerator::generateDailySummaryReport(const DailySummaryQuery& query, const QString& format)
{
    const QVector<QJsonObject> data = queryDailySummary(query);

    if (format == "csv") {
        QStringList columns = {"date", "tag_id", "avg", "min", "max", "first", "last", "count"};
        return toCsv(data, columns);
    }

    return toJson(data);
}

// ============================================================
// CSV Generation
// ============================================================

QByteArray ReportGenerator::toCsv(const QVector<QJsonObject>& data, const QStringList& columns)
{
    QByteArray csv;

    // BOM برای Excel UTF-8
    csv.append("\xEF\xBB\xBF");

    // Header
    csv.append(columns.join(",").toUtf8());
    csv.append("\r\n");

    // Rows
    for (const QJsonObject& row : data) {
        QStringList values;
        for (const QString& col : columns) {
            const QJsonValue val = row.value(col);
            QString strVal;

            if (val.isDouble()) {
                strVal = QString::number(val.toDouble(), 'f', 6);
            } else if (val.isString()) {
                strVal = escapeCsvValue(val.toString());
            } else if (val.isBool()) {
                strVal = val.toBool() ? "true" : "false";
            } else if (val.isNull() || val.isUndefined()) {
                strVal = "";
            }

            values.append(strVal);
        }
        csv.append(values.join(",").toUtf8());
        csv.append("\r\n");
    }

    return csv;
}

QByteArray ReportGenerator::toJson(const QVector<QJsonObject>& data)
{
    QJsonArray array;
    for (const QJsonObject& obj : data) {
        array.append(obj);
    }

    QJsonObject root;
    root.insert("count", data.size());
    root.insert("data", array);

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QString ReportGenerator::escapeCsvValue(const QString& value) const
{
    if (value.contains(',') || value.contains('"') || value.contains('\n')) {
        QString escaped = value;
        escaped.replace("\"", "\"\"");
        return "\"" + escaped + "\"";
    }
    return value;
}
