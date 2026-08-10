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

struct SensorInfo
{
    qint64 tagId = 0;
    int cardIndex = 0;
    int sensorIndex = 0;      // 0-15
    int registerOffset = 0;   // offset در کارت (بر حسب register)
    int registerCount = 1;    // تعداد register برای این سنسور
    QString dataType = "uint16";
    QString wordOrder = "high_first";
    bool valid = false;
};

struct SensorCard
{
    int cardIndex = 0;
    int unitId = 1;
    int function = 3;
    int startAddress = 0;
    int totalRegisters = 16;
    QVector<SensorInfo> sensors;
    bool valid = false;
};

#endif // MODBUSTYPES_H
