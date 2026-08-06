#include "BatchHistorianWriter.h"

#include <QDebug>

BatchHistorianWriter::BatchHistorianWriter(
    DbManager& db,
    int flushIntervalMs,
    int maxBufferSize,
    QObject* parent
)
    : QObject(parent)
    , m_db(&db)
    , m_maxBufferSize(maxBufferSize)
{
    if (m_maxBufferSize <= 0)
    {
        m_maxBufferSize = 1000;
    }

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

    m_timer.start();

    qInfo() << "BatchHistorianWriter started:"
            << "flushIntervalMs=" << flushIntervalMs
            << "maxBufferSize=" << m_maxBufferSize;
}

void BatchHistorianWriter::enqueue(const TagValue& value)
{
    m_buffer.append(value);

    if (m_buffer.size() >= m_maxBufferSize)
    {
        flush();
    }
}

void BatchHistorianWriter::flush()
{
    if (m_buffer.isEmpty())
    {
        return;
    }

    QVector<TagValue> batch;
    batch.swap(m_buffer);

    if (!m_db->insertRawBatch(batch))
    {
        qWarning() << "Historian batch write failed. Re-queueing batch of size" << batch.size();

        m_buffer = batch + m_buffer;

        return;
    }

    m_totalWritten += static_cast<quint64>(batch.size());
    ++m_flushCounter;

    if (m_flushCounter % 10 == 0)
    {
        qInfo() << "Historian batch flushed:"
                << "batchSize=" << batch.size()
                << "totalWritten=" << m_totalWritten;
    }
}
