#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>

#include "../core/Models.h"
#include "DbManager.h"

class BatchHistorianWriter : public QObject
{
public:
    BatchHistorianWriter(
        DbManager& db,
        int flushIntervalMs,
        int maxBufferSize,
        QObject* parent = nullptr
    );

    void enqueue(const TagValue& value);
    void flush();

private:
    DbManager* m_db;

    QVector<TagValue> m_buffer;

    QTimer m_timer;

    int m_maxBufferSize = 1000;

    quint64 m_totalWritten = 0;
    int m_flushCounter = 0;
};
