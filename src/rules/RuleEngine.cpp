#include "RuleEngine.h"

#include <algorithm>
#include <cmath>

#include <QDebug>

RuleEngine::RuleEngine(
    TagBus& bus,
    DbManager& db,
    const AppConfig& config
)
    : m_bus(bus)
    , m_db(db)
    , m_config(config)
{
    for (const TagDefinition& tag : config.tags)
    {
        m_tags.insert(tag.tagId, tag);
    }

    for (const ThresholdRule& rule : config.rules)
    {
        m_thresholdRulesByTag[rule.tagId].push_back(rule);
    }

    for (const RangeViolationRule& rule : config.rangeViolationRules)
    {
        m_rangeRulesByTag[rule.tagId].push_back(rule);
    }

    for (const RateOfChangeRule& rule : config.rateOfChangeRules)
    {
        m_rateRulesByTag[rule.tagId].push_back(rule);
    }

    for (const StuckValueRule& rule : config.stuckValueRules)
    {
        m_stuckRulesByTag[rule.tagId].push_back(rule);
    }

    for (const BooleanRule& rule : config.booleanRules)
    {
        m_booleanRulesByTag[rule.tagId].push_back(rule);
    }

    m_bus.subscribe("tags/#", [this](const BusMessage& message)
    {
        if (!message.topic.endsWith("/update"))
        {
            return;
        }

        onTagValue(message.value);
    });

    qInfo() << "RuleEngine started:"
            << "thresholdRules=" << config.rules.size()
            << "rangeViolationRules=" << config.rangeViolationRules.size()
            << "rateOfChangeRules=" << config.rateOfChangeRules.size()
            << "stuckValueRules=" << config.stuckValueRules.size()
            << "booleanRules=" << config.booleanRules.size();
}

void RuleEngine::onTagValue(const TagValue& value)
{
    TagAlarmState& state = m_states[value.tagId];

    if (handleBadQuality(value, state))
    {
        return;
    }

    evaluateRangeViolation(value, state);
    evaluateRateOfChange(value, state);
    evaluateStuckValue(value, state);
    evaluateBoolean(value, state);

    const QVector<ThresholdRule>& thresholdRules = m_thresholdRulesByTag.value(value.tagId);

    for (const ThresholdRule& rule : thresholdRules)
    {
        evaluateThresholdLevels(value, rule, state);
    }
}

bool RuleEngine::handleBadQuality(const TagValue& value, TagAlarmState& state)
{
    if (value.quality == Quality::Bad)
    {
        if (!state.badQualitySince.isValid())
        {
            state.badQualitySince = value.timestamp;
        }

        if (!state.badQualityActive &&
            state.badQualitySince.msecsTo(value.timestamp) >= m_config.badQualityDelayMs)
        {
            m_db.raiseAlarm(
                value.tagId,
                "bad_quality",
                alarmSeverityToString(AlarmSeverity::High),
                value.engineeringValue,
                0.0,
                "Tag quality is Bad"
            );

            state.badQualityActive = true;
        }

        return true;
    }

    state.badQualitySince = QDateTime();

    if (state.badQualityActive)
    {
        m_db.clearAlarmByTagAndType(value.tagId, "bad_quality");
        state.badQualityActive = false;
    }

    return false;
}

void RuleEngine::evaluateThresholdLevels(
    const TagValue& value,
    const ThresholdRule& rule,
    TagAlarmState& state
)
{
    const int onDelay = effectiveDelay(rule.onDelayMs, m_config.defaultAlarmOnDelayMs);
    const int offDelay = effectiveDelay(rule.offDelayMs, m_config.defaultAlarmOffDelayMs);

    if (rule.hasHighHigh)
    {
        evaluateHighLevel(
            value,
            state.highHigh,
            rule.highHigh,
            effectiveHysteresis(value.tagId, rule.highHighHysteresis),
            "threshold_highhigh",
            AlarmSeverity::Critical,
            onDelay,
            offDelay
        );
    }

    if (rule.hasHigh)
    {
        evaluateHighLevel(
            value,
            state.high,
            rule.high,
            effectiveHysteresis(value.tagId, rule.highHysteresis),
            "threshold_high",
            AlarmSeverity::High,
            onDelay,
            offDelay
        );
    }

    if (rule.hasLow)
    {
        evaluateLowLevel(
            value,
            state.low,
            rule.low,
            effectiveHysteresis(value.tagId, rule.lowHysteresis),
            "threshold_low",
            AlarmSeverity::High,
            onDelay,
            offDelay
        );
    }

    if (rule.hasLowLow)
    {
        evaluateLowLevel(
            value,
            state.lowLow,
            rule.lowLow,
            effectiveHysteresis(value.tagId, rule.lowLowHysteresis),
            "threshold_lowlow",
            AlarmSeverity::Critical,
            onDelay,
            offDelay
        );
    }
}

void RuleEngine::evaluateHighLevel(
    const TagValue& value,
    AlarmLevelState& levelState,
    double setpoint,
    double hysteresis,
    const QString& alarmType,
    AlarmSeverity severity,
    int onDelayMs,
    int offDelayMs
)
{
    const double clearThreshold = setpoint - hysteresis;

    const bool aboveSetpoint = value.engineeringValue > setpoint;
    const bool belowClear = value.engineeringValue < clearThreshold;

    if (!levelState.active)
    {
        if (aboveSetpoint)
        {
            if (!levelState.pending)
            {
                levelState.pending = true;
                levelState.pendingSince = value.timestamp;
            }

            if (levelState.pendingSince.msecsTo(value.timestamp) >= onDelayMs)
            {
                levelState.active = true;
                levelState.pending = false;
                levelState.pendingSince = QDateTime();

                const QString message =
                    QStringLiteral("Value %1 exceeded %2 threshold %3")
                        .arg(value.engineeringValue)
                        .arg(alarmType)
                        .arg(setpoint);

                m_db.raiseAlarm(
                    value.tagId,
                    alarmType,
                    alarmSeverityToString(severity),
                    value.engineeringValue,
                    setpoint,
                    message
                );
            }
        }
        else
        {
            levelState.pending = false;
            levelState.pendingSince = QDateTime();
        }
    }
    else
    {
        if (belowClear)
        {
            if (!levelState.pending)
            {
                levelState.pending = true;
                levelState.pendingSince = value.timestamp;
            }

            if (levelState.pendingSince.msecsTo(value.timestamp) >= offDelayMs)
            {
                levelState.active = false;
                levelState.pending = false;
                levelState.pendingSince = QDateTime();

                m_db.clearAlarmByTagAndType(value.tagId, alarmType);
            }
        }
        else
        {
            levelState.pending = false;
            levelState.pendingSince = QDateTime();
        }
    }
}

void RuleEngine::evaluateLowLevel(
    const TagValue& value,
    AlarmLevelState& levelState,
    double setpoint,
    double hysteresis,
    const QString& alarmType,
    AlarmSeverity severity,
    int onDelayMs,
    int offDelayMs
)
{
    const double clearThreshold = setpoint + hysteresis;

    const bool belowSetpoint = value.engineeringValue < setpoint;
    const bool aboveClear = value.engineeringValue > clearThreshold;

    if (!levelState.active)
    {
        if (belowSetpoint)
        {
            if (!levelState.pending)
            {
                levelState.pending = true;
                levelState.pendingSince = value.timestamp;
            }

            if (levelState.pendingSince.msecsTo(value.timestamp) >= onDelayMs)
            {
                levelState.active = true;
                levelState.pending = false;
                levelState.pendingSince = QDateTime();

                const QString message =
                    QStringLiteral("Value %1 fell below %2 threshold %3")
                        .arg(value.engineeringValue)
                        .arg(alarmType)
                        .arg(setpoint);

                m_db.raiseAlarm(
                    value.tagId,
                    alarmType,
                    alarmSeverityToString(severity),
                    value.engineeringValue,
                    setpoint,
                    message
                );
            }
        }
        else
        {
            levelState.pending = false;
            levelState.pendingSince = QDateTime();
        }
    }
    else
    {
        if (aboveClear)
        {
            if (!levelState.pending)
            {
                levelState.pending = true;
                levelState.pendingSince = value.timestamp;
            }

            if (levelState.pendingSince.msecsTo(value.timestamp) >= offDelayMs)
            {
                levelState.active = false;
                levelState.pending = false;
                levelState.pendingSince = QDateTime();

                m_db.clearAlarmByTagAndType(value.tagId, alarmType);
            }
        }
        else
        {
            levelState.pending = false;
            levelState.pendingSince = QDateTime();
        }
    }
}

void RuleEngine::evaluateRangeViolation(const TagValue& value, TagAlarmState& state)
{
    const QVector<RangeViolationRule>& rules = m_rangeRulesByTag.value(value.tagId);

    if (rules.isEmpty())
    {
        return;
    }

    for (const RangeViolationRule& rule : rules)
    {
        const bool outOfRange =
            value.engineeringValue < rule.minValue ||
            value.engineeringValue > rule.maxValue;

        if (outOfRange && !state.rangeViolationActive)
        {
            state.rangeViolationActive = true;

            const QString message =
                QStringLiteral("Value %1 is out of range [%2, %3]")
                    .arg(value.engineeringValue)
                    .arg(rule.minValue)
                    .arg(rule.maxValue);

            m_db.raiseAlarm(
                value.tagId,
                "range_violation",
                rule.severity,
                value.engineeringValue,
                rule.maxValue,
                message
            );
        }
        else if (!outOfRange && state.rangeViolationActive)
        {
            state.rangeViolationActive = false;

            m_db.clearAlarmByTagAndType(value.tagId, "range_violation");
        }
    }
}

void RuleEngine::evaluateRateOfChange(const TagValue& value, TagAlarmState& state)
{
    const QVector<RateOfChangeRule>& rules = m_rateRulesByTag.value(value.tagId);

    if (rules.isEmpty())
    {
        return;
    }

    QQueue<QPair<QDateTime, double>>& history = m_valueHistory[value.tagId];

    history.enqueue(qMakePair(value.timestamp, value.engineeringValue));

    for (const RateOfChangeRule& rule : rules)
    {
        const QDateTime windowStart = value.timestamp.addMSecs(-rule.windowMs);

        while (!history.isEmpty() && history.head().first < windowStart)
        {
            history.dequeue();
        }

        if (history.size() < 2)
        {
            continue;
        }

        const QPair<QDateTime, double>& oldest = history.head();
        const QPair<QDateTime, double>& newest = history.last();

        const double timeDeltaMs = static_cast<double>(oldest.first.msecsTo(newest.first));

        if (timeDeltaMs <= 0.0)
        {
            continue;
        }

        const double timeDeltaSeconds = timeDeltaMs / 1000.0;
        const double valueDelta = std::fabs(newest.second - oldest.second);
        const double ratePerSecond = valueDelta / timeDeltaSeconds;

        if (ratePerSecond > rule.maxRatePerSecond && !state.rateOfChangeActive)
        {
            state.rateOfChangeActive = true;

            const QString message =
                QStringLiteral("Rate of change %1 exceeds limit %2 per second")
                    .arg(ratePerSecond)
                    .arg(rule.maxRatePerSecond);

            m_db.raiseAlarm(
                value.tagId,
                "rate_of_change",
                rule.severity,
                ratePerSecond,
                rule.maxRatePerSecond,
                message
            );
        }
        else if (ratePerSecond <= rule.maxRatePerSecond && state.rateOfChangeActive)
        {
            state.rateOfChangeActive = false;

            m_db.clearAlarmByTagAndType(value.tagId, "rate_of_change");
        }
    }
}

void RuleEngine::evaluateStuckValue(const TagValue& value, TagAlarmState& state)
{
    const QVector<StuckValueRule>& rules = m_stuckRulesByTag.value(value.tagId);

    if (rules.isEmpty())
    {
        return;
    }

    for (const StuckValueRule& rule : rules)
    {
        if (!state.hasLastValue)
        {
            state.hasLastValue = true;
            state.lastValue = value.engineeringValue;
            state.lastChangeTime = value.timestamp;
            continue;
        }

        const double diff = std::fabs(value.engineeringValue - state.lastValue);

        if (diff > rule.epsilon)
        {
            state.lastValue = value.engineeringValue;
            state.lastChangeTime = value.timestamp;

            if (state.stuckActive)
            {
                state.stuckActive = false;
                m_db.clearAlarmByTagAndType(value.tagId, "stuck_value");
            }
        }
        else
        {
            const qint64 stuckDuration = state.lastChangeTime.msecsTo(value.timestamp);

            if (stuckDuration >= rule.stuckDurationMs && !state.stuckActive)
            {
                state.stuckActive = true;

                const QString message =
                    QStringLiteral("Value %1 has been stuck for %2 ms")
                        .arg(value.engineeringValue)
                        .arg(stuckDuration);

                m_db.raiseAlarm(
                    value.tagId,
                    "stuck_value",
                    rule.severity,
                    value.engineeringValue,
                    0.0,
                    message
                );
            }
        }
    }
}

void RuleEngine::evaluateBoolean(const TagValue& value, TagAlarmState& state)
{
    const QVector<BooleanRule>& rules = m_booleanRulesByTag.value(value.tagId);

    if (rules.isEmpty())
    {
        return;
    }

    const bool currentValue = value.engineeringValue > 0.5;

    for (const BooleanRule& rule : rules)
    {
        if (!state.hasBooleanState)
        {
            state.hasBooleanState = true;
            state.lastBooleanValue = currentValue;
            state.booleanStateSince = value.timestamp;
            continue;
        }

        if (currentValue != state.lastBooleanValue)
        {
            state.lastBooleanValue = currentValue;
            state.booleanStateSince = value.timestamp;

            if (state.booleanAlarmActive)
            {
                state.booleanAlarmActive = false;
                m_db.clearAlarmByTagAndType(value.tagId, "boolean_state");
            }

            continue;
        }

        const qint64 stateDuration = state.booleanStateSince.msecsTo(value.timestamp);

        if (stateDuration < rule.durationMs)
        {
            continue;
        }

        bool shouldAlarm = false;

        if (currentValue && rule.alarmOnTrue)
        {
            shouldAlarm = true;
        }
        else if (!currentValue && rule.alarmOnFalse)
        {
            shouldAlarm = true;
        }

        if (shouldAlarm && !state.booleanAlarmActive)
        {
            state.booleanAlarmActive = true;

            const QString message =
                QStringLiteral("Boolean state %1 has been active for %2 ms")
                    .arg(currentValue ? "TRUE" : "FALSE")
                    .arg(stateDuration);

            m_db.raiseAlarm(
                value.tagId,
                "boolean_state",
                rule.severity,
                currentValue ? 1.0 : 0.0,
                0.0,
                message
            );
        }
    }
}

double RuleEngine::effectiveHysteresis(qint64 tagId, double ruleHysteresis) const
{
    double hysteresis = m_config.defaultAlarmHysteresis;

    const auto tagIterator = m_tags.find(tagId);

    if (tagIterator != m_tags.end())
    {
        const TagDefinition& tag = tagIterator.value();

        if (tag.alarmHysteresis >= 0.0)
        {
            hysteresis = tag.alarmHysteresis;
        }
    }

    if (ruleHysteresis >= 0.0)
    {
        hysteresis = ruleHysteresis;
    }

    return std::max(m_config.globalMinDeadband, hysteresis);
}

int RuleEngine::effectiveDelay(int ruleDelayMs, int defaultDelayMs) const
{
    if (ruleDelayMs >= 0)
    {
        return ruleDelayMs;
    }

    return defaultDelayMs;
}
