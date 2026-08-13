#include "ModbusCardManager.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

ModbusCardManager::ModbusCardManager()
{
}

void ModbusCardManager::buildCards(const QVector<TagDefinition>& tags)
{
    m_cards.clear();
    m_sensorMap.clear();

    QHash<int, SensorCard> cardMap;

    for (const TagDefinition& tag : tags)
    {
        const SensorInfo sensor = parseTagAddress(tag);

        if (!sensor.valid)
        {
            qWarning() << "ModbusCardManager: invalid address config for tag:"
                       << tag.tagName;
            continue;
        }

        m_sensorMap[tag.tagId] = sensor;

        if (!cardMap.contains(sensor.cardIndex))
        {
            SensorCard card;
            card.cardIndex = sensor.cardIndex;
            card.unitId = 1;
            card.function = 3;
            card.startAddress = sensor.cardIndex * 16;
            card.totalRegisters = 16;      // ✅ همیشه کل کارت خوانده می‌شود
            card.valid = true;
            cardMap[sensor.cardIndex] = card;
        }
        // ✅ اعتبارسنجی: سنسور نباید از مرز ۱۶ اسلات بگذرد
        if (sensor.registerOffset + sensor.registerCount > 16)
        {
            qWarning() << "ModbusCardManager: sensor exceeds card boundary, tag:"
                       << tag.tagName;
            continue;
        }

        cardMap[sensor.cardIndex].sensors.append(sensor);
//        cardMap[sensor.cardIndex].totalRegisters += sensor.registerCount;
    }

    // تبدیل hash به vector و مرتب‌سازی
    for (auto it = cardMap.begin(); it != cardMap.end(); ++it)
    {
        m_cards.append(it.value());
    }

    std::sort(m_cards.begin(), m_cards.end(),
        [](const SensorCard& a, const SensorCard& b)
        {
            return a.cardIndex < b.cardIndex;
        });

    qInfo() << "ModbusCardManager: built" << m_cards.size() << "cards";

    for (const SensorCard& card : m_cards)
    {
        qInfo() << "  Card" << card.cardIndex
                << "- startAddress:" << card.startAddress
                << "- totalRegisters:" << card.totalRegisters
                << "- sensors:" << card.sensors.size();
    }
}

const QVector<SensorCard>& ModbusCardManager::cards() const
{
    return m_cards;
}

const SensorInfo* ModbusCardManager::findSensor(qint64 tagId) const
{
    auto it = m_sensorMap.find(tagId);

    if (it == m_sensorMap.end())
    {
        return nullptr;
    }

    return &it.value();
}

int ModbusCardManager::cardCount() const
{
    return m_cards.size();
}

SensorInfo ModbusCardManager::parseTagAddress(const TagDefinition& tag) const
{
    SensorInfo sensor;
    sensor.tagId = tag.tagId;

    const QJsonDocument doc = QJsonDocument::fromJson(tag.addressConfig.toUtf8());

    if (!doc.isObject())
    {
        return sensor;
    }

    const QJsonObject obj = doc.object();

    // روش 1: card_index و sensor_index مستقیم
    if (obj.contains("card_index") && obj.contains("sensor_index"))
    {
        sensor.cardIndex = obj.value("card_index").toInt(0);
        sensor.sensorIndex = obj.value("sensor_index").toInt(0);
    }
    // روش 2: محاسبه از روی address
    else if (obj.contains("address"))
    {
        const int address = obj.value("address").toInt(-1);

        if (address < 0)
        {
            return sensor;
        }

        sensor.cardIndex = address / 16;
        sensor.sensorIndex = address % 16;
    }
    else
    {
        return sensor;
    }

    sensor.dataType = obj.value("data_type").toString("uint16");
    sensor.wordOrder = obj.value("word_order").toString("high_first");

    // محاسبه registerCount بر اساس dataType
    if (sensor.dataType == "int16" || sensor.dataType == "uint16")
    {
        sensor.registerCount = 1;
    }
    else if (sensor.dataType == "int32" || sensor.dataType == "uint32" ||
             sensor.dataType == "float32" || sensor.dataType == "float")
    {
        sensor.registerCount = 2;
    }
    else
    {
        return sensor;
    }

    // محاسبه registerOffset
    sensor.registerOffset = sensor.sensorIndex;
    sensor.valid = true;
    return sensor;
}
