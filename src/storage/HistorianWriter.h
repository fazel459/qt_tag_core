#pragma once

#include "../tagbus/TagBus.h"
#include "DbManager.h"

class HistorianWriter
{
public:
    HistorianWriter(TagBus& bus, DbManager& db)
        : m_db(&db)
    {
        bus.subscribe("tags/#", [this](const BusMessage& message)
        {
            m_db->insertRaw(message.value);
            m_db->upsertCurrent(message.value);
        });
    }

private:
    DbManager* m_db;
};