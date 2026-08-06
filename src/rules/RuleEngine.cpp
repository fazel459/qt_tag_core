#include "RuleEngine.h"

#include <QDebug>

RuleEngine::RuleEngine(
    TagBus& bus,
    DbManager& db,
    const QVector<ThresholdRule>& rules
)
    : m_bus(bus)
    , m_db(db)
    , m_rules(rules)
{
    m_bus.subscribe("tags/#", [this](const BusMessage& message)
    {
        onTagValue(message.value);
    });
}

void RuleEngine::onTagValue(const TagValue& value)
{
    // Bad Quality rule
    if (value.quality == Quality::Bad)
    {
        if (!m_activeBadQualityAlarms.contains(value.tagId))
        {
            m_db.insertAlarm(
                value.tagId,
                "bad_quality",
                "major",
                value.engineeringValue,
                0.0,
                "Tag quality is Bad"
            );

            m_activeBadQualityAlarms.insert(value.tagId);
        }

        return;
    }

    if (m_activeBadQualityAlarms.contains(value.tagId))
    {
        m_db.clearActiveAlarms(value.tagId, "bad_quality");
        m_activeBadQualityAlarms.remove(value.tagId);
    }

    // Threshold rules
    for (const ThresholdRule& rule : m_rules)
    {
        if (rule.tagId != value.tagId)
        {
            continue;
        }

        if (rule.hasHigh)
        {
            if (value.engineeringValue > rule.high)
            {
                if (!m_activeHighAlarms.contains(value.tagId))
                {
                    const QString message =
                        QStringLiteral("Engineering value %1 exceeded high threshold %2")
                            .arg(value.engineeringValue)
                            .arg(rule.high);

                    m_db.insertAlarm(
                        value.tagId,
                        "threshold_high",
                        "high",
                        value.engineeringValue,
                        rule.high,
                        message
                    );

                    m_activeHighAlarms.insert(value.tagId);
                }
            }
            else
            {
                if (m_activeHighAlarms.contains(value.tagId))
                {
                    m_db.clearActiveAlarms(value.tagId, "threshold_high");
                    m_activeHighAlarms.remove(value.tagId);
                }
            }
        }

        if (rule.hasLow)
        {
            if (value.engineeringValue < rule.low)
            {
                if (!m_activeLowAlarms.contains(value.tagId))
                {
                    const QString message =
                        QStringLiteral("Engineering value %1 fell below low threshold %2")
                            .arg(value.engineeringValue)
                            .arg(rule.low);

                    m_db.insertAlarm(
                        value.tagId,
                        "threshold_low",
                        "low",
                        value.engineeringValue,
                        rule.low,
                        message
                    );

                    m_activeLowAlarms.insert(value.tagId);
                }
            }
            else
            {
                if (m_activeLowAlarms.contains(value.tagId))
                {
                    m_db.clearActiveAlarms(value.tagId, "threshold_low");
                    m_activeLowAlarms.remove(value.tagId);
                }
            }
        }
    }
}