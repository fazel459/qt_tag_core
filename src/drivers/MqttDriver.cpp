#include "MqttDriver.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QHostAddress>
#include <cmath>
#include <limits>

#include "../scaling/ScalingEngine.h"

MqttDriver::MqttDriver(
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
    , m_client(nullptr)
{
    const QJsonDocument connectionDoc = QJsonDocument::fromJson(driver.connectionConfig.toUtf8());

    if (connectionDoc.isObject())
    {
        const QJsonObject connectionObj = connectionDoc.object();

        m_host = connectionObj.value("host").toString("localhost");
        m_port = connectionObj.value("port").toInt(1883);
        m_clientId = connectionObj.value("client_id").toString("tag_core_client");
        m_username = connectionObj.value("username").toString();
        m_password = connectionObj.value("password").toString();
    }

    for (const TagDefinition& tag : tags)
    {
        m_tagMap.insert(tag.tagId, tag);

        const QJsonDocument addressDoc = QJsonDocument::fromJson(tag.addressConfig.toUtf8());

        if (addressDoc.isObject())
        {
            const QJsonObject addressObj = addressDoc.object();
            const QString topic = addressObj.value("topic").toString();

            if (!topic.isEmpty())
            {
                m_topicToTagId.insert(topic, tag.tagId);
            }
        }
    }

    qInfo() << "MqttDriver created:"
            << driver.name
            << "host:" << m_host
            << "port:" << m_port
            << "tags:" << m_topicToTagId.size();
}

MqttDriver::~MqttDriver()
{
    stop();

    if (m_client != nullptr)
    {
        delete m_client;
        m_client = nullptr;
    }
}

bool MqttDriver::start()
{
    m_client = new QMQTT::Client(QHostAddress(m_host), static_cast<quint16>(m_port), this);

    if (!m_username.isEmpty())
    {
        m_client->setUsername(m_username);
    }

    if (!m_password.isEmpty())
    {
        m_client->setPassword(m_password.toUtf8());
    }

    m_client->setClientId(m_clientId);
    m_client->setKeepAlive(60);

    QObject::connect(m_client, &QMQTT::Client::connected, this, &MqttDriver::onConnected);
    QObject::connect(m_client, &QMQTT::Client::disconnected, this, &MqttDriver::onDisconnected);
    QObject::connect(m_client, &QMQTT::Client::received, this, &MqttDriver::onMessageReceived);

    connectToBroker();

    return true;
}

void MqttDriver::stop()
{
    if (m_client != nullptr && m_connected)
    {
        m_client->disconnectFromHost();
        m_connected = false;
    }
}

bool MqttDriver::isConnected() const
{
    return m_connected;
}

void MqttDriver::connectToBroker()
{
    if (m_client == nullptr)
    {
        return;
    }

    qInfo() << "MqttDriver: connecting to broker:"
            << m_host << ":" << m_port;

    m_client->connectToHost();
}

void MqttDriver::onConnected()
{
    m_connected = true;

    qInfo() << "MqttDriver: connected to broker:"
            << m_host << ":" << m_port;

    subscribeToTopics();
}

void MqttDriver::onDisconnected()
{
    m_connected = false;

    qWarning() << "MqttDriver: disconnected from broker";

    QTimer::singleShot(3000, [this]()
    {
        connectToBroker();
    });
}

void MqttDriver::subscribeToTopics()
{
    for (auto it = m_topicToTagId.begin(); it != m_topicToTagId.end(); ++it)
    {
        const QString& topic = it.key();

        m_client->subscribe(topic, 1);

        qInfo() << "MqttDriver: subscribed to topic:" << topic;
    }
}

void MqttDriver::onMessageReceived(const QMQTT::Message& message)
{
    const QString topic = message.topic();
    const QByteArray payload = message.payload();

    const TagDefinition tag = findTagByTopic(topic);

    if (tag.tagId == 0)
    {
        qWarning() << "MqttDriver: unknown topic:" << topic;
        return;
    }

    const double value = parsePayload(payload, tag);

    if (std::isnan(value))
    {
        qWarning() << "MqttDriver: failed to parse payload for topic:" << topic;
        publishTagValue(tag, 0.0, QDateTime::currentDateTimeUtc(), Quality::Bad);
        return;
    }

    publishTagValue(tag, value, QDateTime::currentDateTimeUtc(), Quality::Good);
}

TagDefinition MqttDriver::findTagByTopic(const QString& topic) const
{
    const auto it = m_topicToTagId.find(topic);

    if (it == m_topicToTagId.end())
    {
        return TagDefinition();
    }

    return m_tagMap.value(it.value());
}

double MqttDriver::parsePayload(const QByteArray& payload, const TagDefinition& tag) const
{
    const QJsonDocument addressDoc = QJsonDocument::fromJson(tag.addressConfig.toUtf8());

    if (!addressDoc.isObject())
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const QJsonObject addressObj = addressDoc.object();

    const QString payloadType = addressObj.value("payload_type").toString("raw").toLower();

    if (payloadType == "raw")
    {
        bool ok = false;
        const double value = QString::fromUtf8(payload).toDouble(&ok);

        if (!ok)
        {
            return std::numeric_limits<double>::quiet_NaN();
        }

        return value;
    }

    if (payloadType == "json")
    {
        QJsonParseError parseError;
        const QJsonDocument payloadDoc = QJsonDocument::fromJson(payload, &parseError);

        if (parseError.error != QJsonParseError::NoError)
        {
            return std::numeric_limits<double>::quiet_NaN();
        }

        if (!payloadDoc.isObject())
        {
            return std::numeric_limits<double>::quiet_NaN();
        }

        const QJsonObject payloadObj = payloadDoc.object();

        const QString valuePath = addressObj.value("value_path").toString("value");

        const double value = payloadObj.value(valuePath).toDouble(std::numeric_limits<double>::quiet_NaN());

        return value;
    }

    return std::numeric_limits<double>::quiet_NaN();
}

void MqttDriver::publishTagValue(
    const TagDefinition& tag,
    double value,
    const QDateTime& timestamp,
    Quality quality
)
{
    TagValue tagValue;

    tagValue.tagId = tag.tagId;
    tagValue.tagName = tag.tagName;
    tagValue.timestamp = timestamp;
    tagValue.rawValue = value;
    tagValue.engineeringValue = ScalingEngine::scale(tag, value);
    tagValue.quality = quality;
    tagValue.source = SourceKind::RealDriver;

    const QString topic = QStringLiteral("tags/%1/raw").arg(tag.tagId);

    m_bus.publish(topic, tagValue);
}
