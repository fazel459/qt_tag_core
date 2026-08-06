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

        return qBound(tag.engMin, result, tag.engMax);
    }
};