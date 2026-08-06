#pragma once

#include <QMap>
#include <QObject>
#include <QTimer>
#include <QVector>

#include "../core/Models.h"

#include "../tagbus/TagBus.h"

class SimulatorDriver : public QObject
{
public:
    SimulatorDriver(
        TagBus& bus,
        const QVector<TagDefinition>& tags,
        int engineeringDecimals = 4,
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

int m_engineeringDecimals = 4;

    quint64 m_sequence = 0;
};
