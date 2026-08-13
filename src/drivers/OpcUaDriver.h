#pragma once

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QVector>
#include <QString>

#include <open62541.h>

#include "../core/Models.h"
#include "../tagbus/TagBus.h"
#include "ITagDriver.h"

class OpcUaDriver : public QObject, public ITagDriver
{
    Q_OBJECT

public:
    OpcUaDriver(TagBus& bus,
                const DriverDefinition& driver,
                const QVector<TagDefinition>& tags,
                const AppConfig& config,
                QObject* parent = nullptr);
    ~OpcUaDriver() override;

    QString driverType() const override { return QStringLiteral("opc_ua"); }

    bool start() override;
    void stop() override;
    bool isConnected() const override;

private:
    void onIterateTimer();
    void onReconnectTimer();
    void onPollTimer();
    void connectToServer();
    void setupSubscription();
    void addMonitoredItems();
    void resetSession();
    void pollAllTags();

    void publishGood(qint64 tagId, double rawValue);
    void publishBad(qint64 tagId);
    double variantToDouble(const UA_Variant& v) const;
    UA_NodeId parseNodeId(const QString& nodeIdStr) const;

    static void onDataChange(UA_Client* client, UA_UInt32 subId, void* subContext,
                             UA_UInt32 monId, void* monContext, UA_DataValue* value);

    TagBus& m_bus;
    DriverDefinition m_driver;
    QVector<TagDefinition> m_tags;
    AppConfig m_config;

    QHash<qint64, TagDefinition> m_tagMap;
    QHash<qint64, QString> m_nodeIds;
    QHash<qint64, TagValue> m_lastValues;

    UA_Client* m_client = nullptr;
    QTimer m_iterateTimer;
    QTimer m_reconnectTimer;
    QTimer m_pollTimer;

    QString m_endpoint = "opc.tcp://127.0.0.1:4840";
    int m_iterateMs = 100;

    bool m_stopped = true;
    bool m_sessionActive = false;
    bool m_subscriptionReady = false;
    bool m_subscriptionTried = false;
    bool m_usePolling = false;
    UA_UInt32 m_subscriptionId = 0;
};
