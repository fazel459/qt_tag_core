#pragma once

#include <QSet>
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
        const QVector<ThresholdRule>& rules
    );

private:
    void onTagValue(const TagValue& value);

    TagBus& m_bus;
    DbManager& m_db;

    QVector<ThresholdRule> m_rules;

    QSet<qint64> m_activeHighAlarms;
    QSet<qint64> m_activeLowAlarms;
    QSet<qint64> m_activeBadQualityAlarms;
};