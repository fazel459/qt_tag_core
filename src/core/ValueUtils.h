#ifndef VALUEUTILS_H
#define VALUEUTILS_H
#pragma once

#include <cmath>

inline double roundToDecimals(double value, int decimals)
{
    if (decimals < 0)
    {
        decimals = 0;
    }

    if (decimals > 12)
    {
        decimals = 12;
    }

    const double factor = std::pow(10.0, decimals);

    return std::round(value * factor) / factor;
}
#endif // VALUEUTILS_H
