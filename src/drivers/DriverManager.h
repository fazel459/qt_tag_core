#ifndef DRIVERMANAGER_H
#define DRIVERMANAGER_H
#pragma once

#include <QVector>

#include "../core/Models.h"
#include "../tagbus/TagBus.h"

#include "ITagDriver.h"

class DriverManager
{
public:
    DriverManager(TagBus& bus, const AppConfig& config);

    ~DriverManager();

    bool startAll();
    void stopAll();

private:
    TagBus& m_bus;
    AppConfig m_config;

    QVector<ITagDriver*> m_drivers;
};
#endif // DRIVERMANAGER_H
