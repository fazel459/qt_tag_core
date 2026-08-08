#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSerialPort>
#include <QTimer>
#include <QVector>

#include "../core/Models.h"
#include "../tagbus/TagBus.h"

#include "ITagDriver.h"
#include "ModbusTypes.h"

class ModbusRtuDriver : public QObject, public ITagDriver
{
public:
    ModbusRtuDriver(
        TagBus& bus,
        const DriverDefinition& driver,
        const QVector<TagDefinition>& tags,
        const AppConfig& config,
        QObject* parent = nullptr
    );

    ~ModbusRtuDriver();

    QString driverType() const override
    {
        return QStringLiteral("modbus_rtu");
    }

    bool start() override;
    void stop() override;

    bool isConnected() const override;

private:
    void openSerialPort();

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

    static quint16 calculateCRC(const QByteArray& data);

    TagBus& m_bus;

    DriverDefinition m_driver;
    QVector<TagDefinition> m_tags;
    AppConfig m_config;

    QHash<qint64, TagDefinition> m_tagMap;
    QHash<qint64, ModbusTagConfig> m_tagConfigs;
    QHash<qint64, TagValue> m_lastValues;

    QSerialPort m_serialPort;

    QTimer m_pollTimer;
    QTimer m_timeoutTimer;

    QByteArray m_readBuffer;

    QQueue<ModbusTagConfig> m_pollQueue;

    ModbusPendingRequest m_pending;
    bool m_waitingResponse = false;

    QString m_portName = "COM1";
    qint32 m_baudRate = 9600;
    int m_dataBits = 8;
    int m_stopBits = 1;
    QString m_parity = "none";

    int m_timeoutMs = 1000;
    int m_defaultUnitId = 1;

    bool m_reconnectScheduled = false;
};
