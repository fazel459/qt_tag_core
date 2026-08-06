#include "RuleEngine.h"

#include <algorithm>

#include <QDebug>

RuleEngine::RuleEngine(
    TagBus& bus,
    DbManager& db,
    const QVector<ThresholdRule>& rules,
    const AppConfig& config
)
    : m_bus(bus)
    , m_db(db)
    , m_rules(rules)
    , m_config(config)
{
    for (const TagDefinition& tag : config.tags)
    {
        m_tags.insert(tag.tagId, tag);
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
            << "globalMinDeadband=" << m_config.globalMinDeadband
            << "defaultAlarmHysteresis=" << m_config.defaultAlarmHysteresis
            << "defaultOnDelayMs=" << m_config.defaultAlarmOnDelayMs
            << "defaultOffDelayMs=" << m_config.defaultAlarmOffDelayMs
            << "badQualityDelayMs=" << m_config.badQualityDelayMs;
}

void RuleEngine::onTagValue(const TagValue& value)
{
    ThresholdState& state = m_states[value.tagId];

    // اگر کیفیت بد باشد، فعلاً ارزیابی threshold را متوقف می‌کنیم.
    if (handleBadQuality(value, state))
    {
        return;
    }

    for (const ThresholdRule& rule : m_rules)
    {
        if (rule.tagId != value.tagId)
        {
            continue;
        }

        handleThreshold(value, rule, state);
    }
}

bool RuleEngine::handleBadQuality(const TagValue& value, ThresholdState& state)
{
    if (value.quality == Quality::Bad)
    {
        if (!state.badQualitySince.isValid())
        {
            state.badQualitySince = value.timestamp;
        }

        // pendingهای threshold را ریست می‌کنیم تا داده بد باعث فعال‌شدن آلارم نشود.
        state.highPendingSince = QDateTime();
        state.lowPendingSince = QDateTime();
        state.highClearPendingSince = QDateTime();
        state.lowClearPendingSince = QDateTime();

        if (!state.badQualityActive &&
            state.badQualitySince.msecsTo(value.timestamp) >= m_config.badQualityDelayMs)
        {
            m_db.insertAlarm(
                value.tagId,
                "bad_quality",
                "major",
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
        m_db.clearActiveAlarms(value.tagId, "bad_quality");
        state.badQualityActive = false;
    }

    return false;
}

void RuleEngine::handleThreshold(
    const TagValue& value,
    const ThresholdRule& rule,
    ThresholdState& state
)
{
    if (rule.hasHigh)
    {
        evaluateHigh(value, rule, state);
    }

    if (rule.hasLow)
    {
        evaluateLow(value, rule, state);
    }
}

void RuleEngine::evaluateHigh(
    const TagValue& value,
    const ThresholdRule& rule,
    ThresholdState& state
)
{
    const double setpoint = rule.high;
    const double hysteresis = effectiveHysteresis(value.tagId, rule.highHysteresis);
    const double clearThreshold = setpoint - hysteresis;

    const int onDelayMs = effectiveDelay(rule.onDelayMs, m_config.defaultAlarmOnDelayMs);
    const int offDelayMs = effectiveDelay(rule.offDelayMs, m_config.defaultAlarmOffDelayMs);

    const bool aboveSetpoint = value.engineeringValue > setpoint;
    const bool belowClear = value.engineeringValue < clearThreshold;

    if (!state.highActive)
    {
        state.highClearPendingSince = QDateTime();

        if (aboveSetpoint)
        {
            if (!state.highPendingSince.isValid())
            {
                state.highPendingSince = value.timestamp;
            }

            if (state.highPendingSince.msecsTo(value.timestamp) >= onDelayMs)
            {
                state.highActive = true;
                state.highPendingSince = QDateTime();

                const QString message =
                    QStringLiteral("Engineering value %1 exceeded high threshold %2 for %3 ms")
                        .arg(value.engineeringValue)
                        .arg(setpoint)
                        .arg(onDelayMs);

                m_db.insertAlarm(
                    value.tagId,
                    "threshold_high",
                    "high",
                    value.engineeringValue,
                    setpoint,
                    message
                );
            }
        }
        else
        {
            state.highPendingSince = QDateTime();
        }
    }
    else
    {
        state.highPendingSince = QDateTime();

        if (belowClear)
        {
            if (!state.highClearPendingSince.isValid())
            {
                state.highClearPendingSince = value.timestamp;
            }

            if (state.highClearPendingSince.msecsTo(value.timestamp) >= offDelayMs)
            {
                state.highActive = false;
                state.highClearPendingSince = QDateTime();

                m_db.clearActiveAlarms(value.tagId, "threshold_high");
            }
        }
        else
        {
            state.highClearPendingSince = QDateTime();
        }
    }
}

void RuleEngine::evaluateLow(
    const TagValue& value,
    const ThresholdRule& rule,
    ThresholdState& state
)
{
    const double setpoint = rule.low;
    const double hysteresis = effectiveHysteresis(value.tagId, rule.lowHysteresis);
    const double clearThreshold = setpoint + hysteresis;

    const int onDelayMs = effectiveDelay(rule.onDelayMs, m_config.defaultAlarmOnDelayMs);
    const int offDelayMs = effectiveDelay(rule.offDelayMs, m_config.defaultAlarmOffDelayMs);

    const bool belowSetpoint = value.engineeringValue < setpoint;
    const bool aboveClear = value.engineeringValue > clearThreshold;

    if (!state.lowActive)
    {
        state.lowClearPendingSince = QDateTime();

        if (belowSetpoint)
        {
            if (!state.lowPendingSince.isValid())
            {
                state.lowPendingSince = value.timestamp;
            }

            if (state.lowPendingSince.msecsTo(value.timestamp) >= onDelayMs)
            {
                state.lowActive = true;
                state.lowPendingSince = QDateTime();

                const QString message =
                    QStringLiteral("Engineering value %1 fell below low threshold %2 for %3 ms")
                        .arg(value.engineeringValue)
                        .arg(setpoint)
                        .arg(onDelayMs);

                m_db.insertAlarm(
                    value.tagId,
                    "threshold_low",
                    "low",
                    value.engineeringValue,
                    setpoint,
                    message
                );
            }
        }
        else
        {
            state.lowPendingSince = QDateTime();
        }
    }
    else
    {
        state.lowPendingSince = QDateTime();

        if (aboveClear)
        {
            if (!state.lowClearPendingSince.isValid())
            {
                state.lowClearPendingSince = value.timestamp;
            }

            if (state.lowClearPendingSince.msecsTo(value.timestamp) >= offDelayMs)
            {
                state.lowActive = false;
                state.lowClearPendingSince = QDateTime();

                m_db.clearActiveAlarms(value.tagId, "threshold_low");
            }
        }
        else
        {
            state.lowClearPendingSince = QDateTime();
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
