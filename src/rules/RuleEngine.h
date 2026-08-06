#pragma once

#include <QDateTime>
#include <QHash>
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
        const QVector<ThresholdRule>& rules,
        const AppConfig& config
    );

private:
    struct ThresholdState
    {
        bool highActive = false;
        bool lowActive = false;

        QDateTime highPendingSince;
        QDateTime lowPendingSince;

        QDateTime highClearPendingSince;
        QDateTime lowClearPendingSince;

        bool badQualityActive = false;
        QDateTime badQualitySince;
    };

    void onTagValue(const TagValue& value);

    bool handleBadQuality(const TagValue& value, ThresholdState& state);

    void handleThreshold(
        const TagValue& value,
        const ThresholdRule& rule,
        ThresholdState& state
    );

    void evaluateHigh(
        const TagValue& value,
        const ThresholdRule& rule,
        ThresholdState& state
    );

    void evaluateLow(
        const TagValue& value,
        const ThresholdRule& rule,
        ThresholdState& state
    );

    double effectiveHysteresis(qint64 tagId, double ruleHysteresis) const;
    int effectiveDelay(int ruleDelayMs, int defaultDelayMs) const;

    TagBus& m_bus;
    DbManager& m_db;

    QVector<ThresholdRule> m_rules;
    AppConfig m_config;

    QHash<qint64, TagDefinition> m_tags;
    QHash<qint64, ThresholdState> m_states;
};
