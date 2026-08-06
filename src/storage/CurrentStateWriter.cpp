#include "CurrentStateWriter.h"

#include <QDebug>

CurrentStateWriter::CurrentStateWriter(
    TagBus& bus,
    DbManager& db,
    int flushIntervalMs,
    QObject* parent
)
    : QObject(parent)
    , m_db(&db)
{
    if (flushIntervalMs <= 0)
    {
        flushIntervalMs = 500;
    }

    m_timer.setInterval(flushIntervalMs);
    m_timer.setParent(this);

    QObject::connect(&m_timer, &QTimer::timeout, [this]()
    {
        flush();
    });

    bus.subscribe("tags/#", [this](const BusMessage& message)
    {
        enqueue(message.value);
    });

    m_timer.start();

    qInfo() << "CurrentStateWriter started:"
            << "flushIntervalMs=" << flushIntervalMs;
}

void CurrentStateWriter::enqueue(const TagValue& value)
{
    m_latestValues[value.tagId] = value;
}

void CurrentStateWriter::flush()
{
    if (m_latestValues.isEmpty())
    {
        return;
    }

    const QVector<TagValue> batch = QVector<TagValue>::fromList(m_latestValues.values());

    m_latestValues.clear();

    if (!m_db->upsertCurrentBatch(batch))
    {
        qWarning() << "Current state batch write failed. Re-queueing latest values.";

        for (const TagValue& value : batch)
        {
            if (!m_latestValues.contains(value.tagId))
            {
                m_latestValues.insert(value.tagId, value);
            }
        }

        return;
    }

    m_totalWritten += static_cast<quint64>(batch.size());
    ++m_flushCounter;

    if (m_flushCounter % 20 == 0)
    {
        qInfo() << "Current state batch flushed:"
                << "tags=" << batch.size()
                << "totalUpserts=" << m_totalWritten;
    }
}
