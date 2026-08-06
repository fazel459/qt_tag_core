#include "FilterProcessor.h"

#include <QDebug>

#include "../core/ValueUtils.h"
#include "SoftwareFilterFactory.h"

FilterProcessor::FilterProcessor(TagBus& bus, const AppConfig& config)
    : m_bus(bus)
    , m_config(config)
{
    for (const TagDefinition& tag : config.tags)
    {
        m_tags.insert(tag.tagId, tag);
        m_filters.insert(tag.tagId, SoftwareFilterFactory::create(tag));
    }

    m_bus.subscribe("tags/#", [this](const BusMessage& message)
    {
        onMessage(message);
    });

    qInfo() << "FilterProcessor started with"
            << m_filters.size()
            << "software filters";
}

FilterProcessor::~FilterProcessor()
{
    qDeleteAll(m_filters);
}

void FilterProcessor::onMessage(const BusMessage& message)
{
    if (!message.topic.endsWith("/raw"))
    {
        return;
    }

    processRawValue(message.value);
}

void FilterProcessor::processRawValue(const TagValue& rawValue)
{
    TagValue outputValue = rawValue;

    ISoftwareFilter* filter = m_filters.value(rawValue.tagId, nullptr);

    double filteredValue = rawValue.engineeringValue;

    if (filter != nullptr)
    {
        if (rawValue.quality != Quality::Good)
        {
            filter->reset();
        }
        else
        {
            filteredValue = filter->apply(rawValue.engineeringValue, rawValue.timestamp);
        }
    }

    outputValue.engineeringValue = roundToDecimals(filteredValue, m_config.engineeringDecimals);
    outputValue.rawValue = roundToDecimals(rawValue.rawValue, m_config.engineeringDecimals);

    const QString topic = QStringLiteral("tags/%1/update").arg(outputValue.tagId);

    m_bus.publish(topic, outputValue);
}
