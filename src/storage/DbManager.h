#pragma once

#include <QSqlDatabase>

#include "../core/Models.h"

class DbManager
{
public:
    bool initialize(const AppConfig& cfg);

    bool upsertTag(const TagDefinition& tag);

    bool insertRaw(const TagValue& value);
    bool upsertCurrent(const TagValue& value);

    bool insertAlarm(
            qint64 tagId,
            const QString& ruleType,
            const QString& severity,
            double value,
            double threshold,
            const QString& message
            );

    bool clearActiveAlarms(qint64 tagId, const QString& ruleType);
    bool writeBatch(const QVector<TagValue>& rawValues,const QVector<TagValue>& latestValues);

    bool insertRawBatch(const QVector<TagValue>& rawValues);
    bool upsertCurrentBatch(const QVector<TagValue>& latestValues);

    int countTags();
    int countRules();

    QVector<TagDefinition> loadTags();
    QVector<ThresholdRule> loadRules();

    bool insertRule(const ThresholdRule& rule);

    QString settingValue(const QString& key, const QString& defaultValue = QString()) const;
    bool setSetting(const QString& key, const QString& value);

    int settingInt(const QString& key, int defaultValue) const;
    double settingDouble(const QString& key, double defaultValue) const;

    QVector<DriverDefinition> loadDrivers();

    qint64 raiseAlarm(
        qint64 tagId,
        const QString& alarmType,
        const QString& severity,
        double value,
        double threshold,
        const QString& message
    );

    bool clearAlarmByTagAndType(qint64 tagId, const QString& alarmType);

    bool acknowledgeAlarm(qint64 alarmId, const QString& userName);

    bool addAlarmEvent(
        qint64 alarmId,
        const QString& eventType,
        const QString& eventData = QString(),
        const QString& userName = QString()
    );

    QVector<RangeViolationRule> loadRangeViolationRules();
    QVector<RateOfChangeRule> loadRateOfChangeRules();
    QVector<StuckValueRule> loadStuckValueRules();
    QVector<BooleanRule> loadBooleanRules();

private:
    bool migrate();

    static QString sourceToString(SourceKind source);

    QSqlDatabase m_db;
};
