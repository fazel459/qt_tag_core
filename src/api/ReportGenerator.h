#ifndef REPORTGENERATOR_H
#define REPORTGENERATOR_H

#include <QObject>
#include <QByteArray>
#include <QVector>
#include <QJsonObject>
#include <QDateTime>
#include <QDate>
#include <QStringList>
#include "../storage/DbManager.h"

struct TagHistoryQuery {
    QVector<qint64> tagIds;
    QDateTime from;
    QDateTime to;
    QString interval = "raw";  // raw, 1 minute, 5 minutes, 1 hour, 1 day
    int limit = 10000;
};

struct AlarmReportQuery {
    QDateTime from;
    QDateTime to;
    QString severity;  // critical, high, medium, low, info
    QString state;     // active, cleared, acknowledged
    qint64 tagId = 0;  // 0 = all tags
    int limit = 1000;
};

struct DailySummaryQuery {
    QVector<qint64> tagIds;
    QDate date;
};

class ReportGenerator : public QObject
{
    Q_OBJECT

public:
    explicit ReportGenerator(DbManager& db, QObject* parent = nullptr);
    ~ReportGenerator() override;

    // Tag History Report
    QVector<QJsonObject> queryTagHistory(const TagHistoryQuery& query);
    QByteArray generateTagHistoryReport(const TagHistoryQuery& query, const QString& format);

    // Alarm Report
    QVector<QJsonObject> queryAlarms(const AlarmReportQuery& query);
    QByteArray generateAlarmReport(const AlarmReportQuery& query, const QString& format);

    // Daily Summary Report
    QVector<QJsonObject> queryDailySummary(const DailySummaryQuery& query);
    QByteArray generateDailySummaryReport(const DailySummaryQuery& query, const QString& format);

private:
    DbManager& m_db;

    // CSV Generation
    QByteArray toCsv(const QVector<QJsonObject>& data, const QStringList& columns);
    QByteArray toJson(const QVector<QJsonObject>& data);

    // Helpers
    QString escapeCsvValue(const QString& value) const;
};

#endif // REPORTGENERATOR_H
