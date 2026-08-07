#ifndef SIMULATORDRIVERADAPTER_H
#define SIMULATORDRIVERADAPTER_H
#pragma once

#include "ITagDriver.h"
#include "SimulatorDriver.h"

class SimulatorDriverAdapter : public ITagDriver
{
public:
    SimulatorDriverAdapter(TagBus& bus, const QVector<TagDefinition>& tags)
        : m_driver(new SimulatorDriver(bus, tags))
    {
    }

    ~SimulatorDriverAdapter() override
    {
        delete m_driver;
    }

    QString driverType() const override
    {
        return QStringLiteral("simulator");
    }

    bool start() override
    {
        m_driver->start();
        return true;
    }

    void stop() override
    {
        m_driver->stop();
    }

    bool isConnected() const override
    {
        return true;
    }

private:
    SimulatorDriver* m_driver;
};
#endif // SIMULATORDRIVERADAPTER_H
