#ifndef MODBUSTCPDRIVER_H
#define MODBUSTCPDRIVER_H
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QTcpSocket>
#include <QTimer>
#include <QVector>

#include "../core/Models.h"
#include "../tagbus/TagBus.h"

#include "ITagDriver.h"

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

class ModbusTcpDriver : public QObject, public ITagDriver
{
public:
    ModbusTcpDriver(
        TagBus& bus,
        const DriverDefinition& driver,
        const QVector<TagDefinition>& tags,
        const AppConfig& config,
        QObject* parent = nullptr
    );

    ~ModbusTcpDriver();

    QString driverType() const override
    {
        return QStringLiteral("modbus_tcp");
    }

    bool start() override;
    void stop() override;

    bool isConnected() const override;

private:
    void connectToDevice();

    void onConnected();
    void onDisconnected();
    void onReadyRead();

    void onPollTimer();
    void onTimeoutTimer();

    void sendNextRequest();
    void sendReadRequest(const ModbusTagConfig& cfg);

    void processFrame(const QByteArray& frame);

    void publishGood(const ModbusTagConfig& cfg, double rawValue);
    void publishBad(qint64 tagId);

    double decodeRegisters(const QByteArray& registers, const ModbusTagConfig& cfg) const;

    ModbusTagConfig parseTagConfig(const TagDefinition& tag) const;

    int registerCount(const QString& dataType) const;

    TagBus& m_bus;

    DriverDefinition m_driver;
    QVector<TagDefinition> m_tags;
    AppConfig m_config;

    QHash<qint64, TagDefinition> m_tagMap;
    QHash<qint64, ModbusTagConfig> m_tagConfigs;
    QHash<qint64, TagValue> m_lastValues;

    QTcpSocket m_socket;

    QTimer m_pollTimer;
    QTimer m_timeoutTimer;

    QByteArray m_readBuffer;

    QQueue<ModbusTagConfig> m_pollQueue;

    ModbusPendingRequest m_pending;
    bool m_waitingResponse = false;

    quint16 m_transactionId = 0;

    QString m_host = "127.0.0.1";
    int m_port = 502;

    int m_timeoutMs = 1000;
    int m_defaultUnitId = 1;

    bool m_reconnectScheduled = false;
    bool m_debug = false;
};
#endif // MODBUSTCPDRIVER_H
