#ifndef FILTERPROCESSOR_H
#define FILTERPROCESSOR_H
#pragma once

#include <QHash>

#include "../core/Models.h"
#include "../tagbus/TagBus.h"

#include "ISoftwareFilter.h"

class FilterProcessor
{
public:
    FilterProcessor(TagBus& bus, const AppConfig& config);

    ~FilterProcessor();

private:
    void onMessage(const BusMessage& message);
    void processRawValue(const TagValue& rawValue);

    TagBus& m_bus;

    AppConfig m_config;

    QHash<qint64, TagDefinition> m_tags;
    QHash<qint64, ISoftwareFilter*> m_filters;
};
#endif // FILTERPROCESSOR_H
