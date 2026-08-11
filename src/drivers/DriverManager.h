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
    bool startDriver(qint64 driverId);
    bool stopDriver(qint64 driverId);

    bool isDriverRunning(qint64 driverId) const;
    QVector<qint64> runningDriverIds() const;

private:
    TagBus& m_bus;
    AppConfig m_config;
    QHash<qint64, ITagDriver*> m_driverMap;
    QVector<ITagDriver*> m_drivers;
};
#endif // DRIVERMANAGER_H
