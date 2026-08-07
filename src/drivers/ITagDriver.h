#ifndef ITAGDRIVER_H
#define ITAGDRIVER_H
#pragma once

#include <QString>

class ITagDriver
{
public:
    virtual ~ITagDriver() = default;

    virtual QString driverType() const = 0;

    virtual bool start() = 0;
    virtual void stop() = 0;

    virtual bool isConnected() const = 0;
};
#endif // ITAGDRIVER_H
