#ifndef DRIVERFACTORY_H
#define DRIVERFACTORY_H
#pragma once

#include "../core/Models.h"
#include "../tagbus/TagBus.h"

#include "ITagDriver.h"

class DriverFactory
{
public:
    static ITagDriver* create(
        const DriverDefinition& driver,
        const QVector<TagDefinition>& tags,
        TagBus& bus,
        const AppConfig& config
    );
};
#endif // DRIVERFACTORY_H
