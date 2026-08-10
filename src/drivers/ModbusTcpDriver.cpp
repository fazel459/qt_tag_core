#include "ModbusTcpDriver.h"

#include <QDataStream>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <cstring>
#include <limits>

#include "../scaling/ScalingEngine.h"

ModbusTcpDriver::ModbusTcpDriver(
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
    m_socket.setParent(this);

    m_pollTimer.setParent(this);
    m_timeoutTimer.setParent(this);

    m_cardManager = new ModbusCardManager();
    m_cardManager->buildCards(m_tags);

    if (m_driver.pollingIntervalMs <= 0)
    {
        m_driver.pollingIntervalMs = 1000;
    }

    m_pollTimer.setInterval(m_driver.pollingIntervalMs);
    m_timeoutTimer.setInterval(200);

    m_bus.subscribe("commands/#", [this](const BusMessage& message)
    {
        if (!message.topic.endsWith("/write"))
        {
            return;
        }

        const TagValue& command = message.value;

        writeValue(command.tagId, command.engineeringValue);
    });

    const QJsonDocument connectionDoc = QJsonDocument::fromJson(driver.connectionConfig.toUtf8());

    if (connectionDoc.isObject())
    {
        const QJsonObject connectionObj = connectionDoc.object();

        m_host = connectionObj.value("host").toString("127.0.0.1");
        m_port = connectionObj.value("port").toInt(502);
        m_timeoutMs = connectionObj.value("timeout_ms").toInt(1000);
        m_defaultUnitId = connectionObj.value("default_unit_id").toInt(1);
        m_debug = connectionObj.value("debug").toBool(false);
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
            qWarning() << "Invalid Modbus address config for tag:" << tag.tagName;
        }
    }

    QObject::connect(&m_socket, &QTcpSocket::connected, [this]()
    {
        onConnected();
    });

    QObject::connect(&m_socket, &QTcpSocket::disconnected, [this]()
    {
        onDisconnected();
    });

    QObject::connect(&m_socket, &QAbstractSocket::stateChanged, [this](QAbstractSocket::SocketState state)
    {
        if (state == QAbstractSocket::UnconnectedState)
        {
            onDisconnected();
        }
    });

    QObject::connect(&m_socket, &QTcpSocket::readyRead, [this]()
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

    qInfo() << "ModbusTcpDriver created:"
            << driver.name
            << "host:" << m_host
            << "port:" << m_port
            << "tags:" << m_tagConfigs.size();
}

ModbusTcpDriver::~ModbusTcpDriver()
{
    stop();
}

bool ModbusTcpDriver::start()
{
    connectToDevice();

    m_pollTimer.start();
    m_timeoutTimer.start();

    return true;
}

void ModbusTcpDriver::stop()
{
    m_pollTimer.stop();
    m_timeoutTimer.stop();

    m_socket.disconnectFromHost();

    m_pollQueue.clear();

    m_waitingResponse = false;
    m_pending.valid = false;
}

bool ModbusTcpDriver::isConnected() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

void ModbusTcpDriver::writeValue(qint64 tagId, double engineeringValue)
{
    if (!isConnected())
    {
        qWarning() << "ModbusTcpDriver: cannot write, not connected. tagId=" << tagId;
        return;
    }

    const ModbusTagConfig cfg = m_tagConfigs.value(tagId);

    if (!cfg.valid)
    {
        qWarning() << "ModbusTcpDriver: invalid config for write. tagId=" << tagId;
        return;
    }

    const TagDefinition tag = m_tagMap.value(tagId);

    const double rawValue = ScalingEngine::reverseScale(tag, engineeringValue);

    const QString dataType = cfg.dataType.trimmed().toLower();

    if (dataType == "coil")
    {
        const bool boolValue = rawValue > 0.5;
        sendWriteSingleCoil(cfg, boolValue);
    }
    else if (dataType == "int16" || dataType == "uint16")
    {
        const quint16 regValue = static_cast<quint16>(rawValue);
        sendWriteSingleRegister(cfg, regValue);
    }
    else if (dataType == "int32" || dataType == "uint32" || dataType == "float32" || dataType == "float")
    {
        QVector<quint16> registers;

        if (dataType == "float32" || dataType == "float")
        {
            const float floatValue = static_cast<float>(rawValue);
            quint32 bits = 0;
            std::memcpy(&bits, &floatValue, sizeof(bits));

            const quint16 highWord = static_cast<quint16>(bits >> 16);
            const quint16 lowWord = static_cast<quint16>(bits & 0xFFFF);

            if (cfg.wordOrder.trimmed().toLower() == "low_first")
            {
                registers.append(lowWord);
                registers.append(highWord);
            }
            else
            {
                registers.append(highWord);
                registers.append(lowWord);
            }
        }
        else
        {
            const quint32 intValue = static_cast<quint32>(rawValue);

            const quint16 highWord = static_cast<quint16>(intValue >> 16);
            const quint16 lowWord = static_cast<quint16>(intValue & 0xFFFF);

            if (cfg.wordOrder.trimmed().toLower() == "low_first")
            {
                registers.append(lowWord);
                registers.append(highWord);
            }
            else
            {
                registers.append(highWord);
                registers.append(lowWord);
            }
        }

        sendWriteMultipleRegisters(cfg, registers);
    }
    else
    {
        qWarning() << "ModbusTcpDriver: unsupported data type for write:" << dataType;
    }
}

void ModbusTcpDriver::sendWriteSingleCoil(const ModbusTagConfig& cfg, bool value)
{
    ++m_transactionId;

    if (m_transactionId == 0)
    {
        ++m_transactionId;
    }

    QByteArray frame;

    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    const quint16 length = 6;
    const quint16 coilValue = value ? 0xFF00 : 0x0000;

    stream << m_transactionId;
    stream << static_cast<quint16>(0);
    stream << length;
    stream << static_cast<quint8>(cfg.unitId);
    stream << static_cast<quint8>(0x05);
    stream << static_cast<quint16>(cfg.address);
    stream << coilValue;

    m_socket.write(frame);

    qInfo() << "ModbusTcpDriver: FC5 Write Single Coil:"
            << "address=" << cfg.address
            << "value=" << value;
}

void ModbusTcpDriver::sendWriteSingleRegister(const ModbusTagConfig& cfg, quint16 value)
{
    ++m_transactionId;

    if (m_transactionId == 0)
    {
        ++m_transactionId;
    }

    QByteArray frame;

    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    const quint16 length = 6;

    stream << m_transactionId;
    stream << static_cast<quint16>(0);
    stream << length;
    stream << static_cast<quint8>(cfg.unitId);
    stream << static_cast<quint8>(0x06);
    stream << static_cast<quint16>(cfg.address);
    stream << value;

    m_socket.write(frame);

    qInfo() << "ModbusTcpDriver: FC6 Write Single Register:"
            << "address=" << cfg.address
            << "value=" << value;
}

void ModbusTcpDriver::sendWriteMultipleRegisters(const ModbusTagConfig& cfg, const QVector<quint16>& values)
{
    ++m_transactionId;

    if (m_transactionId == 0)
    {
        ++m_transactionId;
    }

    QByteArray frame;

    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    const quint16 quantity = static_cast<quint16>(values.size());
    const quint8 byteCount = static_cast<quint8>(quantity * 2);
    const quint16 length = 7 + byteCount;

    stream << m_transactionId;
    stream << static_cast<quint16>(0);
    stream << length;
    stream << static_cast<quint8>(cfg.unitId);
    stream << static_cast<quint8>(0x10);
    stream << static_cast<quint16>(cfg.address);
    stream << quantity;
    stream << byteCount;

    for (const quint16& value : values)
    {
        stream << value;
    }

    m_socket.write(frame);

    qInfo() << "ModbusTcpDriver: FC16 Write Multiple Registers:"
            << "address=" << cfg.address
            << "quantity=" << quantity;
}

void ModbusTcpDriver::connectToDevice()
{
    if (m_socket.state() != QAbstractSocket::UnconnectedState)
    {
        return;
    }

    qInfo() << "Connecting to Modbus TCP device:"
            << m_host << ":" << m_port;

    m_socket.connectToHost(m_host, static_cast<quint16>(m_port));
}

void ModbusTcpDriver::onConnected()
{
    qInfo() << "Connected to Modbus TCP device:"
            << m_host << ":" << m_port;
}

void ModbusTcpDriver::onDisconnected()
{
    if (m_waitingResponse)
    {
        publishBad(m_pending.tagId);
    }

    m_waitingResponse = false;
    m_pending.valid = false;

    m_readBuffer.clear();

    if (m_reconnectScheduled)
    {
        return;
    }

    m_reconnectScheduled = true;

    QTimer::singleShot(3000, [this]()
    {
        m_reconnectScheduled = false;
        connectToDevice();
    });
}

void ModbusTcpDriver::onReadyRead()
{
    m_readBuffer.append(m_socket.readAll());

    if (m_debug)
    {
        qDebug() << "Modbus RX:" << m_readBuffer.toHex();
    }

    while (m_readBuffer.size() >= 7)
    {
        const uchar* bytes = reinterpret_cast<const uchar*>(m_readBuffer.constData());

        const quint16 length = static_cast<quint16>((bytes[4] << 8) | bytes[5]);
        const int frameSize = 6 + static_cast<int>(length);

        if (m_readBuffer.size() < frameSize)
        {
            break;
        }

        const QByteArray frame = m_readBuffer.left(frameSize);
        m_readBuffer.remove(0, frameSize);

        processFrame(frame);
    }
}

void ModbusTcpDriver::onPollTimer()
{
    if (!isConnected())
    {
        return;
    }

    if (m_waitingResponse)
    {
        return;
    }

    if (m_cardManager->cardCount() > 0)
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

void ModbusTcpDriver::onTimeoutTimer()
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
        qWarning() << "Modbus TCP timeout for tagId:" << m_pending.tagId;

        publishBad(m_pending.tagId);
         if (m_debug){
        qWarning() << "Modbus timeout:"
                   << "transactionId=" << m_pending.transactionId
                   << "tagId=" << m_pending.tagId
                   << "timeoutMs=" << m_timeoutMs;
         }
        m_waitingResponse = false;
        m_pending.valid = false;

        sendNextRequest();
    }
}

void ModbusTcpDriver::pollCards()
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

void ModbusTcpDriver::sendCardReadRequest(const SensorCard& card)
{
    ++m_transactionId;

    if (m_transactionId == 0)
    {
        ++m_transactionId;
    }

    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    const quint16 length = 6;

    stream << m_transactionId;
    stream << static_cast<quint16>(0);
    stream << length;
    stream << static_cast<quint8>(card.unitId);
    stream << static_cast<quint8>(card.function);
    stream << static_cast<quint16>(card.startAddress);
    stream << static_cast<quint16>(card.totalRegisters);

    m_socket.write(frame);

    m_currentCard = card;

    m_pending.transactionId = m_transactionId;
    m_pending.tagId = card.cardIndex;  // برای تشخیص، tagId را cardIndex می‌گذاریم
    m_pending.sentAt = QDateTime::currentDateTimeUtc();
    m_pending.valid = true;

    m_waitingResponse = true;

    qInfo() << "ModbusTcpDriver: card read request:"
            << "card=" << card.cardIndex
            << "startAddress=" << card.startAddress
            << "totalRegisters=" << card.totalRegisters
            << "sensors=" << card.sensors.size();
}

void ModbusTcpDriver::processCardResponse(const QByteArray& registers, const SensorCard& card)
{
    int offset = 0;

    for (const SensorInfo& sensor : card.sensors)
    {
        const int byteCount = sensor.registerCount * 2;

        const QByteArray sensorRegisters = registers.mid(offset, byteCount);

        // ساخت ModbusTagConfig برای decode
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

    qInfo() << "ModbusTcpDriver: card response processed:"
            << "card=" << card.cardIndex
            << "sensors=" << card.sensors.size();
}


void ModbusTcpDriver::sendNextRequest()
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

void ModbusTcpDriver::sendReadRequest(const ModbusTagConfig& cfg)
{
    const int quantity = registerCount(cfg.dataType);

    if (quantity <= 0)
    {
        qWarning() << "Unsupported Modbus data type:" << cfg.dataType
                   << "for tagId:" << cfg.tagId;
        return;
    }

    ++m_transactionId;

    if (m_transactionId == 0)
    {
        ++m_transactionId;
    }

    QByteArray frame;

    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    const quint16 length = 6;

    stream << m_transactionId;
    stream << static_cast<quint16>(0);
    stream << length;
    stream << static_cast<quint8>(cfg.unitId);
    stream << static_cast<quint8>(cfg.function);
    stream << static_cast<quint16>(cfg.address);
    stream << static_cast<quint16>(quantity);

    m_socket.write(frame);

    m_pending.transactionId = m_transactionId;
    m_pending.tagId = cfg.tagId;
    m_pending.dataType = cfg.dataType;
    m_pending.wordOrder = cfg.wordOrder;
    m_pending.sentAt = QDateTime::currentDateTimeUtc();
    m_pending.valid = true;

    m_waitingResponse = true;

    if (m_debug)
    {
        qDebug() << "Modbus TX:" << frame.toHex();
        qDebug() << "Modbus request sent:"
                 << "transactionId=" << m_transactionId
                 << "tagId=" << cfg.tagId
                 << "function=" << cfg.function
                 << "address=" << cfg.address;
    }
}

void ModbusTcpDriver::processFrame(const QByteArray& frame)
{
    if (frame.size() < 9)
    {
        qWarning() << "ModbusTcpDriver: frame too small:" << frame.size();
        return;
    }

    if (m_debug)
    {
        qDebug() << "ModbusTcpDriver: processFrame:" << frame.toHex();
    }

    const uchar* bytes = reinterpret_cast<const uchar*>(frame.constData());

    const quint16 transactionId = static_cast<quint16>((bytes[0] << 8) | bytes[1]);
    const quint16 protocolId = static_cast<quint16>((bytes[2] << 8) | bytes[3]);
    const quint16 length = static_cast<quint16>((bytes[4] << 8) | bytes[5]);
    const quint8 unitId = bytes[6];
    const quint8 functionCode = bytes[7];

    if (protocolId != 0)
    {
        qWarning() << "ModbusTcpDriver: invalid protocol ID:" << protocolId;
        return;
    }

    if (!m_waitingResponse || !m_pending.valid)
    {
        if (m_debug)
        {
            qDebug() << "ModbusTcpDriver: ignoring response (not waiting or pending invalid)";
        }
        return;
    }

    if (transactionId != m_pending.transactionId)
    {
        if (m_debug)
        {
            qDebug() << "ModbusTcpDriver: transaction mismatch:"
                     << "response=" << transactionId
                     << "pending=" << m_pending.transactionId;
        }
        return;
    }

    // بررسی Exception Response
    if ((functionCode & 0x80) != 0)
    {
        const quint8 exceptionCode = bytes[8];

        qWarning() << "ModbusTcpDriver: exception response:"
                   << "functionCode=" << functionCode
                   << "exceptionCode=" << exceptionCode;

        // اگر card response بود، همه سنسورهای کارت را bad کن
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

    // بررسی اینکه آیا این یک Card Response است
    if (m_currentCard.valid && m_currentCard.cardIndex == m_pending.tagId)
    {
        // Card Response
        const quint8 byteCount = bytes[8];

        if (frame.size() < 9 + byteCount)
        {
            qWarning() << "ModbusTcpDriver: card frame incomplete:"
                       << "expected=" << (9 + byteCount)
                       << "actual=" << frame.size();

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

        const QByteArray registers = frame.mid(9, byteCount);

        if (m_debug)
        {
            qDebug() << "ModbusTcpDriver: card response:"
                     << "card=" << m_currentCard.cardIndex
                     << "byteCount=" << byteCount
                     << "registers=" << registers.toHex();
        }

        processCardResponse(registers, m_currentCard);

        m_currentCard.valid = false;
        m_waitingResponse = false;
        m_pending.valid = false;

        // ادامه polling کارت‌ها
        pollCards();
        return;
    }

    // Response معمولی (یک تگ)
    const quint8 byteCount = bytes[8];

    if (frame.size() < 9 + byteCount)
    {
        qWarning() << "ModbusTcpDriver: frame incomplete:"
                   << "expected=" << (9 + byteCount)
                   << "actual=" << frame.size();

        publishBad(m_pending.tagId);

        m_waitingResponse = false;
        m_pending.valid = false;

        sendNextRequest();
        return;
    }

    const QByteArray registers = frame.mid(9, byteCount);

    const qint64 tagId = m_pending.tagId;

    m_waitingResponse = false;
    m_pending.valid = false;

    const ModbusTagConfig cfg = m_tagConfigs.value(tagId);

    if (!cfg.valid)
    {
        qWarning() << "ModbusTcpDriver: invalid config for tagId:" << tagId;
        publishBad(tagId);
        sendNextRequest();
        return;
    }

    const double rawValue = decodeRegisters(registers, cfg);

    if (std::isnan(rawValue))
    {
        qWarning() << "ModbusTcpDriver: decode failed for tagId:" << tagId;
        publishBad(tagId);
    }
    else
    {
        publishGood(cfg, rawValue);
    }

    sendNextRequest();
}

void ModbusTcpDriver::publishGood(const ModbusTagConfig& cfg, double rawValue)
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
    if (m_debug){
    qInfo() << "Modbus good value:"
            << "tagId=" << cfg.tagId
            << "raw=" << rawValue;
    }
}

void ModbusTcpDriver::publishBad(qint64 tagId)
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

int ModbusTcpDriver::registerCount(const QString& dataType) const
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

double ModbusTcpDriver::decodeRegisters(const QByteArray& registers, const ModbusTagConfig& cfg) const
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

ModbusTagConfig ModbusTcpDriver::parseTagConfig(const TagDefinition& tag) const
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

    if (cfg.dataType == "coil")
    {
        cfg.function = 5;
    }

    cfg.valid = true;

    return cfg;
}
