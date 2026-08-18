#pragma once
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QVector>
#include "../tagbus/TagBus.h"

class RealtimeCache
{
public:
    explicit RealtimeCache(TagBus& bus)
    {
        bus.subscribe("tags/#", [this](const BusMessage& message)
        {
            if (!message.topic.endsWith("/update")) return;
            QMutexLocker locker(&m_mutex);
            m_latestValues[message.value.tagId] = message.value;
        });
    }

    TagValue get(qint64 tagId) const
    {
        QMutexLocker locker(&m_mutex);
        return m_latestValues.value(tagId);
    }

    QVector<TagValue> getMany(const QVector<qint64>& ids) const
    {
        QMutexLocker locker(&m_mutex);
        QVector<TagValue> out;
        for (qint64 id : ids)
            if (m_latestValues.contains(id))
                out.append(m_latestValues.value(id));
        return out;
    }

private:
    mutable QMutex m_mutex;
    QHash<qint64, TagValue> m_latestValues;
};
