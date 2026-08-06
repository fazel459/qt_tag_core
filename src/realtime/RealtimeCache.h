#pragma once

#include <QHash>

#include "../tagbus/TagBus.h"

class RealtimeCache
{
public:
    explicit RealtimeCache(TagBus& bus)
    {
        bus.subscribe("tags/#", [this](const BusMessage& message)
        {
            m_latestValues[message.value.tagId] = message.value;
        });
    }

private:
    QHash<qint64, TagValue> m_latestValues;
};