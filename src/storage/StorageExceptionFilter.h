#ifndef STORAGEEXCEPTIONFILTER_H
#define STORAGEEXCEPTIONFILTER_H
#pragma once

#include <QDateTime>
#include <QHash>

#include "../core/Models.h"
#include "../tagbus/TagBus.h"
#include "BatchHistorianWriter.h"

class StorageExceptionFilter
{
public:
    StorageExceptionFilter(
        TagBus& bus,
        BatchHistorianWriter& historian,
        const AppConfig& config
    );

private:
    struct State
    {
        bool initialized = false;
        double lastStoredValue = 0.0;
        Quality lastStoredQuality = Quality::Bad;
        QDateTime lastStoredTime;
    };

    void onTagValue(const TagValue& value);

    bool shouldStore(const TagDefinition& tag, const TagValue& value);

    double effectiveStorageDeadband(const TagDefinition& tag) const;
    int effectiveHeartbeatIntervalMs(const TagDefinition& tag) const;

    TagBus& m_bus;
    BatchHistorianWriter* m_historian;

    AppConfig m_config;

    QHash<qint64, TagDefinition> m_tags;
    QHash<qint64, State> m_states;
};
#endif // STORAGEEXCEPTIONFILTER_H
