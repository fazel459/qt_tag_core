#include "ModbusRtuDriver.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSerialPortInfo>

#include <cmath>
#include <cstring>
#include <limits>

#include "../scaling/ScalingEngine.h"

ModbusRtuDriver::ModbusRtuDriver(
    TagBus& bus,
    const DriverDefinition& driver,
    const QVector<TagDefinition>& tags,
    const AppConfig& config,
    QObject* parent
)
    : QObject(parent)
    , m_bus(bus)
    , m_driver(driver)
    , m_tags(tags)
    , m_config(config)
{
    m_serialPort.setParent(this);

    m_pollTimer.setParent(this);
    m_timeoutTimer.setParent(this);



    if (m_driver.pollingIntervalMs <= 0)
    {
        m_driver.pollingIntervalMs = 1000;
    }

    m_pollTimer.setInterval(m_driver.pollingIntervalMs);
    m_timeoutTimer.setInterval(200);

    const QJsonDocument connectionDoc = QJsonDocument::fromJson(driver.connectionConfig.toUtf8());

    if (connectionDoc.isObject())
    {
        const QJsonObject connectionObj = connectionDoc.object();

        m_portName = connectionObj.value("port").toString("COM1");
        m_baudRate = connectionObj.value("baud_rate").toInt(9600);
        m_dataBits = connectionObj.value("data_bits").toInt(8);
        m_stopBits = connectionObj.value("stop_bits").toInt(1);
        m_parity = connectionObj.value("parity").toString("none").toLower();
        m_timeoutMs = connectionObj.value("timeout_ms").toInt(1000);
        m_defaultUnitId = connectionObj.value("default_unit_id").toInt(1);
    }

    for (const TagDefinition& tag : tags)
    {
        m_tagMap.insert(tag.tagId, tag);

        const ModbusTagConfig modbusTag = parseTagConfig(tag);

        if (modbusTag.valid)
        {
            m_tagConfigs.insert(tag.tagId, modbusTag);
        }
        else
        {
            qWarning() << "Invalid Modbus RTU address config for tag:" << tag.tagName;
        }
    }

    QObject::connect(&m_serialPort, &QSerialPort::readyRead, [this]()
    {
        onReadyRead();
    });

    QObject::connect(&m_pollTimer, &QTimer::timeout, [this]()
    {
        onPollTimer();
    });

    QObject::connect(&m_timeoutTimer, &QTimer::timeout, [this]()
    {
        onTimeoutTimer();
    });

    m_cardManager = new ModbusCardManager();
    m_cardManager->buildCards(m_tags);

    qInfo() << "ModbusRtuDriver created:"
            << driver.name
            << "port:" << m_portName
            << "baudRate:" << m_baudRate
            << "tags:" << m_tagConfigs.size()
            << "cards:" << m_cardManager->cardCount();
}

ModbusRtuDriver::~ModbusRtuDriver()
{
    stop();
    if (m_cardManager != nullptr)
    {
        delete m_cardManager;
        m_cardManager = nullptr;
    }
}

bool ModbusRtuDriver::start()
{
    openSerialPort();

    m_pollTimer.start();
    m_timeoutTimer.start();

    return true;
}

void ModbusRtuDriver::stop()
{
    m_pollTimer.stop();
    m_timeoutTimer.stop();

    m_serialPort.close();

    m_pollQueue.clear();

    m_waitingResponse = false;
    m_pending.valid = false;
}

bool ModbusRtuDriver::isConnected() const
{
    return m_serialPort.isOpen();
}

void ModbusRtuDriver::openSerialPort()
{
    if (m_serialPort.isOpen())
    {
        return;
    }

    m_serialPort.setPortName(m_portName);
    m_serialPort.setBaudRate(m_baudRate);

    if (m_dataBits == 7)
    {
        m_serialPort.setDataBits(QSerialPort::Data7);
    }
    else
    {
        m_serialPort.setDataBits(QSerialPort::Data8);
    }

    if (m_stopBits == 2)
    {
        m_serialPort.setStopBits(QSerialPort::TwoStop);
    }
    else
    {
        m_serialPort.setStopBits(QSerialPort::OneStop);
    }

    if (m_parity == "even")
    {
        m_serialPort.setParity(QSerialPort::EvenParity);
    }
    else if (m_parity == "odd")
    {
        m_serialPort.setParity(QSerialPort::OddParity);
    }
    else
    {
        m_serialPort.setParity(QSerialPort::NoParity);
    }

    m_serialPort.setFlowControl(QSerialPort::NoFlowControl);

    if (m_serialPort.open(QIODevice::ReadWrite))
    {
        qInfo() << "ModbusRtuDriver: serial port opened:" << m_portName;
    }
    else
    {
        qWarning() << "ModbusRtuDriver: failed to open serial port:" << m_portName
                   << m_serialPort.errorString();

        QTimer::singleShot(3000, [this]()
        {
            openSerialPort();
        });
    }
}

void ModbusRtuDriver::onReadyRead()
{
    m_readBuffer.append(m_serialPort.readAll());

    while (true)
    {
        const int expectedSize = expectedResponseSize(m_readBuffer);

        if (expectedSize < 0)
        {
            // هنوز داده کافی نداریم
            break;
        }

        if (m_readBuffer.size() < expectedSize)
        {
            // هنوز فریم کامل نشده
            break;
        }

        const QByteArray frame = m_readBuffer.left(expectedSize);
        m_readBuffer.remove(0, expectedSize);

        processFrame(frame);
    }
}

void ModbusRtuDriver::onPollTimer()
{
    if (!isConnected())
    {
        return;
    }

    if (m_waitingResponse)
    {
        return;
    }

    if (m_cardManager != nullptr && m_cardManager->cardCount() > 0)
    {
        // Batch Read برای کارت‌ها
        pollCards();
    }
    else
    {
        // روش قدیمی: خواندن تگ‌ها یکی‌یکی
        if (m_pollQueue.isEmpty())
        {
            for (const ModbusTagConfig& cfg : m_tagConfigs)
            {
                m_pollQueue.enqueue(cfg);
            }
        }

        sendNextRequest();
    }
}

void ModbusRtuDriver::onTimeoutTimer()
{
    if (!m_waitingResponse)
    {
        return;
    }

    if (!m_pending.valid)
    {
        return;
    }

    if (m_pending.sentAt.msecsTo(QDateTime::currentDateTimeUtc()) >= m_timeoutMs)
    {
        qWarning() << "ModbusRtuDriver: timeout for tagId:" << m_pending.tagId;

        publishBad(m_pending.tagId);

        m_waitingResponse = false;
        m_pending.valid = false;

        sendNextRequest();
    }
}

void ModbusRtuDriver::sendNextRequest()
{
    if (m_waitingResponse)
    {
        return;
    }

    if (m_pollQueue.isEmpty())
    {
        return;
    }

    const ModbusTagConfig cfg = m_pollQueue.dequeue();

    sendReadRequest(cfg);
}

void ModbusRtuDriver::sendReadRequest(const ModbusTagConfig& cfg)
{
    const int quantity = registerCount(cfg.dataType);

    if (quantity <= 0)
    {
        return;
    }

    QByteArray frame;

    frame.append(static_cast<char>(cfg.unitId));
    frame.append(static_cast<char>(cfg.function));
    frame.append(static_cast<char>((cfg.address >> 8) & 0xFF));
    frame.append(static_cast<char>(cfg.address & 0xFF));
    frame.append(static_cast<char>((quantity >> 8) & 0xFF));
    frame.append(static_cast<char>(quantity & 0xFF));

    const quint16 crc = calculateCRC(frame);

    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));

    m_serialPort.write(frame);

    m_pending.transactionId = 0;
    m_pending.tagId = cfg.tagId;
    m_pending.dataType = cfg.dataType;
    m_pending.wordOrder = cfg.wordOrder;
    m_pending.sentAt = QDateTime::currentDateTimeUtc();
    m_pending.valid = true;

    m_waitingResponse = true;
}

void ModbusRtuDriver::processFrame(const QByteArray& frame)
{
    if (frame.size() < 5)
    {
        qWarning() << "ModbusRtuDriver: frame too small:" << frame.size();
        return;
    }

    const int frameSize = frame.size();

    const QByteArray receivedData = frame.left(frameSize - 2);
    const QByteArray receivedCrcBytes = frame.mid(frameSize - 2);

    const quint16 receivedCrc = static_cast<quint16>(
        (static_cast<uchar>(receivedCrcBytes[1]) << 8) |
        static_cast<uchar>(receivedCrcBytes[0])
    );

    const quint16 calculatedCrc = calculateCRC(receivedData);

    if (receivedCrc != calculatedCrc)
    {
        qWarning() << "ModbusRtuDriver: CRC mismatch:"
                   << "received=" << receivedCrc
                   << "calculated=" << calculatedCrc;
        return;
    }

    if (!m_waitingResponse || !m_pending.valid)
    {
        return;
    }

    const uchar* bytes = reinterpret_cast<const uchar*>(receivedData.constData());

    const quint8 slaveAddress = bytes[0];
    const quint8 functionCode = bytes[1];

    // بررسی Exception Response
    if ((functionCode & 0x80) != 0)
    {
        const quint8 exceptionCode = bytes[2];

        qWarning() << "ModbusRtuDriver: exception response:"
                   << "functionCode=" << functionCode
                   << "exceptionCode=" << exceptionCode;

        if (m_currentCard.valid)
        {
            for (const SensorInfo& sensor : m_currentCard.sensors)
            {
                publishBad(sensor.tagId);
            }

            m_currentCard.valid = false;
        }
        else
        {
            publishBad(m_pending.tagId);
        }

        m_waitingResponse = false;
        m_pending.valid = false;

        sendNextRequest();
        return;
    }

    // بررسی Card Response
    if (m_currentCard.valid && m_currentCard.cardIndex == m_pending.tagId)
    {
        const quint8 byteCount = bytes[2];

        if (receivedData.size() < 3 + byteCount)
        {
            qWarning() << "ModbusRtuDriver: card frame incomplete:"
                       << "expected=" << (3 + byteCount)
                       << "actual=" << receivedData.size();

            for (const SensorInfo& sensor : m_currentCard.sensors)
            {
                publishBad(sensor.tagId);
            }

            m_currentCard.valid = false;
            m_waitingResponse = false;
            m_pending.valid = false;

            sendNextRequest();
            return;
        }

        const QByteArray registers = receivedData.mid(3, byteCount);

        processCardResponse(registers, m_currentCard);

        m_currentCard.valid = false;
        m_waitingResponse = false;
        m_pending.valid = false;

        pollCards();
        return;
    }

    // Response معمولی (یک تگ)
    const quint8 byteCount = bytes[2];

    if (receivedData.size() < 3 + byteCount)
    {
        qWarning() << "ModbusRtuDriver: frame incomplete:"
                   << "expected=" << (3 + byteCount)
                   << "actual=" << receivedData.size();

        publishBad(m_pending.tagId);

        m_waitingResponse = false;
        m_pending.valid = false;

        sendNextRequest();
        return;
    }

    const QByteArray registers = receivedData.mid(3, byteCount);

    const qint64 tagId = m_pending.tagId;

    m_waitingResponse = false;
    m_pending.valid = false;

    const ModbusTagConfig cfg = m_tagConfigs.value(tagId);

    if (!cfg.valid)
    {
        qWarning() << "ModbusRtuDriver: invalid config for tagId:" << tagId;
        publishBad(tagId);
        sendNextRequest();
        return;
    }

    const double rawValue = decodeRegisters(registers, cfg);

    if (std::isnan(rawValue))
    {
        qWarning() << "ModbusRtuDriver: decode failed for tagId:" << tagId;
        publishBad(tagId);
    }
    else
    {
        publishGood(cfg, rawValue);
    }

    sendNextRequest();
}

void ModbusRtuDriver::publishGood(const ModbusTagConfig& cfg, double rawValue)
{
    const TagDefinition tag = m_tagMap.value(cfg.tagId);

    TagValue value;

    value.tagId = cfg.tagId;
    value.tagName = tag.tagName;
    value.timestamp = QDateTime::currentDateTimeUtc();

    value.rawValue = rawValue;
    value.engineeringValue = ScalingEngine::scale(tag, rawValue);

    value.quality = Quality::Good;
    value.source = SourceKind::RealDriver;

    m_lastValues[cfg.tagId] = value;

    const QString topic = QStringLiteral("tags/%1/raw").arg(cfg.tagId);

    m_bus.publish(topic, value);
}

void ModbusRtuDriver::publishBad(qint64 tagId)
{
    TagValue value = m_lastValues.value(tagId);

    const TagDefinition tag = m_tagMap.value(tagId);

    value.tagId = tagId;
    value.tagName = tag.tagName;
    value.timestamp = QDateTime::currentDateTimeUtc();
    value.quality = Quality::Bad;
    value.source = SourceKind::RealDriver;

    m_lastValues[tagId] = value;

    const QString topic = QStringLiteral("tags/%1/raw").arg(tagId);

    m_bus.publish(topic, value);
}

int ModbusRtuDriver::registerCount(const QString& dataType) const
{
    const QString type = dataType.trimmed().toLower();

    if (type == "int16" || type == "uint16")
    {
        return 1;
    }

    if (type == "int32" || type == "uint32" || type == "float32" || type == "float")
    {
        return 2;
    }

    return 0;
}

double ModbusRtuDriver::decodeRegisters(const QByteArray& registers, const ModbusTagConfig& cfg) const
{
    const QString type = cfg.dataType.trimmed().toLower();

    if (type == "int16" || type == "uint16")
    {
        if (registers.size() < 2)
        {
            return std::numeric_limits<double>::quiet_NaN();
        }

        const uchar* b = reinterpret_cast<const uchar*>(registers.constData());

        const quint16 reg0 = static_cast<quint16>((b[0] << 8) | b[1]);

        if (type == "uint16")
        {
            return static_cast<double>(reg0);
        }

        return static_cast<double>(static_cast<qint16>(reg0));
    }

    if (type == "int32" || type == "uint32" || type == "float32" || type == "float")
    {
        if (registers.size() < 4)
        {
            return std::numeric_limits<double>::quiet_NaN();
        }

        const uchar* b = reinterpret_cast<const uchar*>(registers.constData());

        const quint16 reg0 = static_cast<quint16>((b[0] << 8) | b[1]);
        const quint16 reg1 = static_cast<quint16>((b[2] << 8) | b[3]);

        quint16 highWord = reg0;
        quint16 lowWord = reg1;

        if (cfg.wordOrder.trimmed().toLower() == "low_first")
        {
            highWord = reg1;
            lowWord = reg0;
        }

        const quint32 combined = (static_cast<quint32>(highWord) << 16) | static_cast<quint32>(lowWord);

        if (type == "uint32")
        {
            return static_cast<double>(combined);
        }

        if (type == "int32")
        {
            return static_cast<double>(static_cast<qint32>(combined));
        }

        float floatValue = 0.0f;

        std::memcpy(&floatValue, &combined, sizeof(floatValue));

        return static_cast<double>(floatValue);
    }

    return std::numeric_limits<double>::quiet_NaN();
}

ModbusTagConfig ModbusRtuDriver::parseTagConfig(const TagDefinition& tag) const
{
    ModbusTagConfig cfg;

    cfg.tagId = tag.tagId;

    const QJsonDocument doc = QJsonDocument::fromJson(tag.addressConfig.toUtf8());

    if (!doc.isObject())
    {
        return cfg;
    }

    const QJsonObject obj = doc.object();

    cfg.unitId = obj.value("unit_id").toInt(m_defaultUnitId);

    const QString function = obj.value("function").toString("holding_register").trimmed().toLower();

    if (function == "holding_register" || function == "holding" || function == "fc3")
    {
        cfg.function = 3;
    }
    else if (function == "input_register" || function == "input" || function == "fc4")
    {
        cfg.function = 4;
    }
    else
    {
        return cfg;
    }

    cfg.address = obj.value("address").toInt(-1);

    if (cfg.address < 0)
    {
        return cfg;
    }

    cfg.dataType = obj.value("data_type").toString("uint16");
    cfg.wordOrder = obj.value("word_order").toString("high_first");

    cfg.valid = true;

    return cfg;
}

quint16 ModbusRtuDriver::calculateCRC(const QByteArray& data)
{
    quint16 crc = 0xFFFF;

    for (int i = 0; i < data.size(); ++i)
    {
        crc ^= static_cast<uchar>(data[i]);

        for (int j = 0; j < 8; ++j)
        {
            if (crc & 0x0001)
            {
                crc = (crc >> 1) ^ 0xA001;
            }
            else
            {
                crc = crc >> 1;
            }
        }
    }

    return crc;
}

void ModbusRtuDriver::buildCardGroups()
{
    if (m_cardManager != nullptr)
    {
        m_cardManager->buildCards(m_tags);
    }
}

void ModbusRtuDriver::pollCards()
{
    if (m_cardPollQueue.isEmpty())
    {
        const QVector<SensorCard>& cards = m_cardManager->cards();

        for (const SensorCard& card : cards)
        {
            m_cardPollQueue.enqueue(card);
        }
    }

    if (m_cardPollQueue.isEmpty())
    {
        return;
    }

    const SensorCard card = m_cardPollQueue.dequeue();

    sendCardReadRequest(card);
}

void ModbusRtuDriver::sendCardReadRequest(const SensorCard& card)
{
    QByteArray frame;

    frame.append(static_cast<char>(card.unitId));
    frame.append(static_cast<char>(card.function));
    frame.append(static_cast<char>((card.startAddress >> 8) & 0xFF));
    frame.append(static_cast<char>(card.startAddress & 0xFF));
    frame.append(static_cast<char>((card.totalRegisters >> 8) & 0xFF));
    frame.append(static_cast<char>(card.totalRegisters & 0xFF));

    const quint16 crc = calculateCRC(frame);

    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));

    m_serialPort.write(frame);

    m_currentCard = card;

    m_pending.transactionId = 0;
    m_pending.tagId = card.cardIndex;
    m_pending.sentAt = QDateTime::currentDateTimeUtc();
    m_pending.valid = true;

    m_waitingResponse = true;

    qInfo() << "ModbusRtuDriver: card read request:"
            << "card=" << card.cardIndex
            << "startAddress=" << card.startAddress
            << "totalRegisters=" << card.totalRegisters
            << "sensors=" << card.sensors.size();
}

void ModbusRtuDriver::processCardResponse(const QByteArray& registers, const SensorCard& card)
{
    int offset = 0;

    for (const SensorInfo& sensor : card.sensors)
    {
        const int byteCount = sensor.registerCount * 2;

        const QByteArray sensorRegisters = registers.mid(offset, byteCount);

        ModbusTagConfig cfg;
        cfg.tagId = sensor.tagId;
        cfg.dataType = sensor.dataType;
        cfg.wordOrder = sensor.wordOrder;

        const double rawValue = decodeRegisters(sensorRegisters, cfg);

        if (std::isnan(rawValue))
        {
            publishBad(sensor.tagId);
        }
        else
        {
            publishGood(cfg, rawValue);
        }

        offset += byteCount;
    }

    qInfo() << "ModbusRtuDriver: card response processed:"
            << "card=" << card.cardIndex
            << "sensors=" << card.sensors.size();
}

int ModbusRtuDriver::expectedResponseSize(const QByteArray& buffer) const
{
    if (buffer.size() < 2)
    {
        return -1;
    }

    const uchar* bytes = reinterpret_cast<const uchar*>(buffer.constData());

    const quint8 functionCode = bytes[1];

    // Exception Response: 5 bytes
    if ((functionCode & 0x80) != 0)
    {
        return 5;
    }

    // Read Response: نیاز به byte count داریم
    if (buffer.size() < 3)
    {
        return -1;
    }

    const quint8 byteCount = bytes[2];

    // 1 (address) + 1 (function) + 1 (byte count) + byteCount + 2 (CRC)
    return 5 + byteCount;
}
