#pragma once

#include <cmath>

#include <QDateTime>
#include <QHash>

#include "../core/Models.h"

class DeadbandFilter
{
public:
    bool shouldPublish(const TagDefinition& tag, const TagValue& value)
    {
        State& state = m_states[value.tagId];

        if (!state.initialized)
        {
            state.initialized = true;
            state.lastEngineeringValue = value.engineeringValue;
            state.lastQuality = value.quality;
            state.lastPublishTime = value.timestamp;
            return true;
        }

        bool publish = false;

        if (tag.deadband <= 0.0)
        {
            publish = true;
        }
        else if (value.quality != state.lastQuality)
        {
            publish = true;
        }
        else if (std::fabs(value.engineeringValue - state.lastEngineeringValue) >= tag.deadband)
        {
            publish = true;
        }
        else if (state.lastPublishTime.isValid() &&
                 state.lastPublishTime.msecsTo(value.timestamp) >= 30000)
        {
            // Heartbeat: every 30 seconds publish even if no significant change.
            publish = true;
        }

        if (publish)
        {
            state.lastEngineeringValue = value.engineeringValue;
            state.lastQuality = value.quality;
            state.lastPublishTime = value.timestamp;
        }

        return publish;
    }

private:
    struct State
    {
        bool initialized = false;
        double lastEngineeringValue = 0.0;
        Quality lastQuality = Quality::Bad;
        QDateTime lastPublishTime;
    };

    QHash<qint64, State> m_states;
};