#ifndef MQTTDRIVER_H
#define MQTTDRIVER_H
#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVector>

#include "../core/Models.h"
#include "../tagbus/TagBus.h"

#include "ITagDriver.h"

#include "qmqtt.h"

class MqttDriver : public QObject, public ITagDriver
{
public:
    MqttDriver(
        TagBus& bus,
        const DriverDefinition& driver,
        const QVector<TagDefinition>& tags,
        const AppConfig& config,
        QObject* parent = nullptr
    );

    ~MqttDriver();

    QString driverType() const override
    {
        return QStringLiteral("mqtt");
    }

    bool start() override;
    void stop() override;

    bool isConnected() const override;

private:
    void connectToBroker();

    void onConnected();
    void onDisconnected();
    void onMessageReceived(const QMQTT::Message& message);

    void subscribeToTopics();

    void publishTagValue(const TagDefinition& tag, double value, const QDateTime& timestamp, Quality quality);

    TagDefinition findTagByTopic(const QString& topic) const;

    double parsePayload(const QByteArray& payload, const TagDefinition& tag) const;

    TagBus& m_bus;

    DriverDefinition m_driver;
    QVector<TagDefinition> m_tags;
    AppConfig m_config;

    QHash<qint64, TagDefinition> m_tagMap;
    QHash<QString, qint64> m_topicToTagId;

    QMQTT::Client* m_client;

    QString m_host = "localhost";
    int m_port = 1883;
    QString m_clientId = "tag_core_client";
    QString m_username;
    QString m_password;

    bool m_connected = false;
};
#endif // MQTTDRIVER_H
