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
        if (!sensor.valid) {
            qWarning() << "ModbusCardManager: invalid address config for tag:" << tag.tagName;
            continue;
        }
        m_sensorMap[tag.tagId] = sensor;

        // ✅ کارت = slave id
        if (!cardMap.contains(sensor.unitId)) {
            SensorCard card;
            card.cardIndex = sensor.unitId;
            card.unitId = sensor.unitId;          // ✅ درخواست به slave درست می‌رود
            card.function = 3;
            card.startAddress = sensor.baseAddress;
            card.totalRegisters = 16;             // ✅ همیشه کل کارت
            card.valid = true;
            cardMap[sensor.unitId] = card;
        }
        cardMap[sensor.unitId].sensors.append(sensor);
    }

    for (auto it = cardMap.begin(); it != cardMap.end(); ++it)
        m_cards.append(it.value());

    std::sort(m_cards.begin(), m_cards.end(),
              [](const SensorCard& a, const SensorCard& b)
              { return a.cardIndex < b.cardIndex; });

    qInfo() << "ModbusCardManager: built" << m_cards.size() << "cards";
    for (const SensorCard& card : m_cards) {
        qInfo() << "  Card(slave)" << card.cardIndex
                << "- unitId:" << card.unitId
                << "- startAddress:" << card.startAddress
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
    if (!doc.isObject()) return sensor;
    const QJsonObject obj = doc.object();

    // ۱) slave id کارت (هر کارت = یک slave)
    int unitId = obj.value("unit_id").toInt(-1);
    if (unitId < 0) unitId = obj.value("card_index").toInt(-1);   // سازگاری قدیمی
    if (unitId < 1 || unitId > 247) return sensor;
    sensor.unitId = unitId;

    // ۲) اسلات سنسور داخل کارت (0..15)
    int slot = -1;
    if (obj.contains("sensor_index"))     slot = obj.value("sensor_index").toInt(-1);
    else if (obj.contains("address"))     slot = obj.value("address").toInt(-1) % 16;
    if (slot < 0 || slot > 15) return sensor;
    sensor.sensorIndex = slot;

    // ۳) رجیستر شروع بلوک داخل slave (اختیاری، پیش‌فرض 0)
    sensor.baseAddress = obj.value("start_address").toInt(0);

    sensor.dataType = obj.value("data_type").toString("uint16");
    sensor.wordOrder = obj.value("word_order").toString("high_first");

    if (sensor.dataType == "int16" || sensor.dataType == "uint16")
        sensor.registerCount = 1;
    else if (sensor.dataType == "int32" || sensor.dataType == "uint32" ||
             sensor.dataType == "float32" || sensor.dataType == "float")
        sensor.registerCount = 2;
    else
        return sensor;

    if (sensor.sensorIndex + sensor.registerCount > 16) return sensor;  // سرریز اسلات

    sensor.registerOffset = sensor.sensorIndex;   // ✅ اسلاتی
    sensor.valid = true;
    return sensor;
}
