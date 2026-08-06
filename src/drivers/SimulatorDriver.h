#pragma once

#include <QMap>
#include <QObject>
#include <QTimer>
#include <QVector>

#include "../core/Models.h"
#include "../ingestion/DeadbandFilter.h"
#include "../tagbus/TagBus.h"

class SimulatorDriver : public QObject
{
public:
    SimulatorDriver(
        TagBus& bus,
        const QVector<TagDefinition>& tags,
        QObject* parent = nullptr
    );

    void start();
    void stop();

private:
    void tick();
    double generateRaw(const TagDefinition& tag);

    TagBus& m_bus;
    QVector<TagDefinition> m_tags;

    QTimer m_timer;

    QMap<qint64, double> m_phase;
    QMap<qint64, double> m_ramp;

    DeadbandFilter m_deadbandFilter;

    quint64 m_sequence = 0;
};