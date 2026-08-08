#pragma once

#include <QDateTime>
#include <QHash>
#include <QQueue>
#include <QPair>
#include <QVector>

#include "../core/Models.h"
#include "../storage/DbManager.h"
#include "../tagbus/TagBus.h"

class RuleEngine
{
public:
    RuleEngine(
        TagBus& bus,
        DbManager& db,
        const AppConfig& config
    );

private:
    struct AlarmLevelState
    {
        bool active = false;
        bool pending = false;
        QDateTime pendingSince;
    };

    struct TagAlarmState
    {
        AlarmLevelState lowLow;
        AlarmLevelState low;
        AlarmLevelState high;
        AlarmLevelState highHigh;

        bool badQualityActive = false;
        QDateTime badQualitySince;

        bool rangeViolationActive = false;

        bool rateOfChangeActive = false;

        bool stuckActive = false;
        double lastValue = 0.0;
        QDateTime lastChangeTime;
        bool hasLastValue = false;

        bool booleanAlarmActive = false;
        bool lastBooleanValue = false;
        QDateTime booleanStateSince;
        bool hasBooleanState = false;
    };

    void onTagValue(const TagValue& value);

    bool handleBadQuality(const TagValue& value, TagAlarmState& state);

    void evaluateThresholdLevels(
        const TagValue& value,
        const ThresholdRule& rule,
        TagAlarmState& state
    );

    void evaluateHighLevel(
        const TagValue& value,
        AlarmLevelState& levelState,
        double setpoint,
        double hysteresis,
        const QString& alarmType,
        AlarmSeverity severity,
        int onDelayMs,
        int offDelayMs
    );

    void evaluateLowLevel(
        const TagValue& value,
        AlarmLevelState& levelState,
        double setpoint,
        double hysteresis,
        const QString& alarmType,
        AlarmSeverity severity,
        int onDelayMs,
        int offDelayMs
    );

    void evaluateRangeViolation(const TagValue& value, TagAlarmState& state);
    void evaluateRateOfChange(const TagValue& value, TagAlarmState& state);
    void evaluateStuckValue(const TagValue& value, TagAlarmState& state);
    void evaluateBoolean(const TagValue& value, TagAlarmState& state);

    double effectiveHysteresis(qint64 tagId, double ruleHysteresis) const;
    int effectiveDelay(int ruleDelayMs, int defaultDelayMs) const;
    void publishAlarmEvent(
        qint64 tagId,
        const QString& alarmType,
        const QString& severity,
        const QString& state,
        double value,
        double threshold,
        const QString& message
    );

    TagBus& m_bus;
    DbManager& m_db;

    AppConfig m_config;

    QHash<qint64, TagDefinition> m_tags;
    QHash<qint64, TagAlarmState> m_states;

    QHash<qint64, QVector<ThresholdRule>> m_thresholdRulesByTag;
    QHash<qint64, QVector<RangeViolationRule>> m_rangeRulesByTag;
    QHash<qint64, QVector<RateOfChangeRule>> m_rateRulesByTag;
    QHash<qint64, QVector<StuckValueRule>> m_stuckRulesByTag;
    QHash<qint64, QVector<BooleanRule>> m_booleanRulesByTag;

    QHash<qint64, QQueue<QPair<QDateTime, double>>> m_valueHistory;
};
