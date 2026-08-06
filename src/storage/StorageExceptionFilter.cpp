#include "StorageExceptionFilter.h"

#include <cmath>
#include <algorithm>

#include <QDebug>

StorageExceptionFilter::StorageExceptionFilter(
    TagBus& bus,
    BatchHistorianWriter& historian,
    const AppConfig& config
)
    : m_bus(bus)
    , m_historian(&historian)
    , m_config(config)
{
    for (const TagDefinition& tag : config.tags)
    {
        m_tags.insert(tag.tagId, tag);
    }

    m_bus.subscribe("tags/#", [this](const BusMessage& message)
    {
        if (!message.topic.endsWith("/update"))
        {
            return;
        }

        onTagValue(message.value);
    });

    qInfo() << "StorageExceptionFilter started:"
            << "globalMinDeadband=" << m_config.globalMinDeadband
            << "defaultHeartbeatMs=" << m_config.defaultHeartbeatIntervalMs;
}

void StorageExceptionFilter::onTagValue(const TagValue& value)
{
    const auto tagIterator = m_tags.find(value.tagId);

    if (tagIterator == m_tags.end())
    {
        // اگر تگ ناشناخته بود، فعلاً آن را ذخیره تاریخی نمی‌کنیم.
        return;
    }

    const TagDefinition& tag = tagIterator.value();

    if (!shouldStore(tag, value))
    {
        return;
    }

    State& state = m_states[value.tagId];

    state.initialized = true;
    state.lastStoredValue = value.engineeringValue;
    state.lastStoredQuality = value.quality;
    state.lastStoredTime = value.timestamp;

    m_historian->enqueue(value);
}

bool StorageExceptionFilter::shouldStore(
    const TagDefinition& tag,
    const TagValue& value
)
{
    State& state = m_states[value.tagId];

    if (!state.initialized)
    {
        return true;
    }

    if (value.quality != state.lastStoredQuality)
    {
        return true;
    }

    const double deadband = effectiveStorageDeadband(tag);

    if (deadband <= 0.0)
    {
        return true;
    }

    if (std::fabs(value.engineeringValue - state.lastStoredValue) >= deadband)
    {
        return true;
    }

    const int heartbeatMs = effectiveHeartbeatIntervalMs(tag);

    if (state.lastStoredTime.isValid() &&
        state.lastStoredTime.msecsTo(value.timestamp) >= heartbeatMs)
    {
        return true;
    }

    return false;
}

double StorageExceptionFilter::effectiveStorageDeadband(const TagDefinition& tag) const
{
    double localDeadband = tag.storageDeadband;

    if (localDeadband < 0.0)
    {
        localDeadband = tag.deadband;
    }

    if (localDeadband < 0.0)
    {
        localDeadband = 0.0;
    }

    return std::max(m_config.globalMinDeadband, localDeadband);
}

int StorageExceptionFilter::effectiveHeartbeatIntervalMs(const TagDefinition& tag) const
{
    if (tag.heartbeatIntervalMs >= 0)
    {
        return tag.heartbeatIntervalMs;
    }

    return m_config.defaultHeartbeatIntervalMs;
}
