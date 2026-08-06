#ifndef SOFTWAREFILTERFACTORY_H
#define SOFTWAREFILTERFACTORY_H
#pragma once

#include "../core/Models.h"
#include "ISoftwareFilter.h"

class SoftwareFilterFactory
{
public:
    static ISoftwareFilter* create(const TagDefinition& tag);
};
#endif // SOFTWAREFILTERFACTORY_H
