#ifndef MODBUSTYPES_H
#define MODBUSTYPES_H
#pragma once

#include <QDateTime>
#include <QString>

struct ModbusTagConfig
{
    qint64 tagId = 0;
    int unitId = 1;
    int function = 3;
    int address = -1;
    QString dataType = "uint16";
    QString wordOrder = "high_first";
    bool valid = false;
};

struct ModbusPendingRequest
{
    quint16 transactionId = 0;
    qint64 tagId = 0;
    QString dataType;
    QString wordOrder;
    QDateTime sentAt;
    bool valid = false;
};
#endif // MODBUSTYPES_H
