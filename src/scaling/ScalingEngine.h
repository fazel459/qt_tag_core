#pragma once

#include <cmath>
#include <limits>

#include <QtGlobal>

#include "../core/Models.h"

class ScalingEngine
{
public:
    static double scale(const TagDefinition& tag, double raw)
    {
        double result = raw;

        if (tag.scalingType == "min_max")
        {
            const double span = tag.rawMax - tag.rawMin;

            if (std::fabs(span) > 1e-9)
            {
                const double normalized = (raw - tag.rawMin) / span;
                result = tag.engMin + normalized * (tag.engMax - tag.engMin);
            }
            else
            {
                result = tag.engMin;
            }
        }
        else
        {
            result = raw * tag.slope + tag.offset;
        }

        if (std::isnan(result))
        {
            result = tag.engMin;
        }

        if (tag.clampEnabled)
        {
            return qBound(tag.engMin, result, tag.engMax);
        }

        return result;
    }

    static double reverseScale(const TagDefinition& tag, double engValue)
    {
        double result = engValue;

        if (tag.scalingType == "min_max")
        {
            const double span = tag.engMax - tag.engMin;

            if (std::fabs(span) > 1e-9)
            {
                const double normalized = (engValue - tag.engMin) / span;
                result = tag.rawMin + normalized * (tag.rawMax - tag.rawMin);
            }
            else
            {
                result = tag.rawMin;
            }
        }
        else
        {
            if (std::fabs(tag.slope) > 1e-9)
            {
                result = (engValue - tag.offset) / tag.slope;
            }
            else
            {
                result = engValue;
            }
        }

        if (std::isnan(result))
        {
            result = tag.rawMin;
        }

        if (tag.clampEnabled)
        {
            return qBound(tag.rawMin, result, tag.rawMax);
        }

        return result;
    }
};
