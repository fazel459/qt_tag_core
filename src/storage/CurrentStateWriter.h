#ifndef CURRENTSTATEWRITER_H
#define CURRENTSTATEWRITER_H
#pragma once

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QVector>

#include "../core/Models.h"
#include "../tagbus/TagBus.h"
#include "DbManager.h"

class CurrentStateWriter : public QObject
{
public:
    CurrentStateWriter(
        TagBus& bus,
        DbManager& db,
        int flushIntervalMs,
        QObject* parent = nullptr
    );

    void enqueue(const TagValue& value);
    void flush();

private:
    DbManager* m_db;

    QHash<qint64, TagValue> m_latestValues;

    QTimer m_timer;

    quint64 m_totalWritten = 0;
    int m_flushCounter = 0;
};
#endif // CURRENTSTATEWRITER_H
