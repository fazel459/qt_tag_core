#ifndef MODBUSCARDMANAGER_H
#define MODBUSCARDMANAGER_H
#pragma once

#include <QHash>
#include <QVector>

#include "../core/Models.h"
#include "ModbusTypes.h"

class ModbusCardManager
{
public:
    ModbusCardManager();

    void buildCards(const QVector<TagDefinition>& tags);

    const QVector<SensorCard>& cards() const;

    const SensorInfo* findSensor(qint64 tagId) const;

    int cardCount() const;

private:
    SensorInfo parseTagAddress(const TagDefinition& tag) const;

    QVector<SensorCard> m_cards;
    QHash<qint64, SensorInfo> m_sensorMap;
};


#endif // MODBUSCARDMANAGER_H
