#ifndef ISOFTWAREFILTER_H
#define ISOFTWAREFILTER_H
#pragma once

#include <QString>
#include <QDateTime>

class ISoftwareFilter
{
public:
    virtual ~ISoftwareFilter() = default;

    virtual void reset() = 0;

    virtual double apply(double newValue, const QDateTime& timestamp) = 0;

    virtual QString name() const = 0;
};
#endif // ISOFTWAREFILTER_H
