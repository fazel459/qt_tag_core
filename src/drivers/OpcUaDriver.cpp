#include "OpcUaDriver.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <limits>

#include "../scaling/ScalingEngine.h"

OpcUaDriver::OpcUaDriver(TagBus& bus,
                         const DriverDefinition& driver,
                         const QVector<TagDefinition>& tags,
                         const AppConfig& config,
                         QObject* parent)
    : QObject(parent)
    , m_bus(bus)
    , m_driver(driver)
    , m_tags(tags)
    , m_config(config)
{
    m_iterateTimer.setParent(this);
    m_reconnectTimer.setParent(this);
    m_pollTimer.setParent(this);

    if (m_driver.pollingIntervalMs <= 0)
        m_driver.pollingIntervalMs = 1000;

    m_pollTimer.setInterval(m_driver.pollingIntervalMs);
    m_reconnectTimer.setSingleShot(true);

    const QJsonDocument doc = QJsonDocument::fromJson(driver.connectionConfig.toUtf8());
    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        m_endpoint = obj.value("endpoint").toString("opc.tcp://127.0.0.1:4840");
        m_iterateMs = obj.value("iterate_ms").toInt(100);
    }

    for (const TagDefinition& tag : tags) {
        m_tagMap.insert(tag.tagId, tag);

        const QJsonDocument addrDoc = QJsonDocument::fromJson(tag.addressConfig.toUtf8());
        if (addrDoc.isObject() && addrDoc.object().contains("node_id")) {
            m_nodeIds.insert(tag.tagId, addrDoc.object().value("node_id").toString());
        } else {
            qWarning() << "OpcUaDriver: missing node_id for tag:" << tag.tagName;
        }
    }

    QObject::connect(&m_iterateTimer, &QTimer::timeout, [this]() { onIterateTimer(); });
    QObject::connect(&m_reconnectTimer, &QTimer::timeout, [this]() { onReconnectTimer(); });
    QObject::connect(&m_pollTimer, &QTimer::timeout, [this]() { onPollTimer(); });

    qInfo() << "OpcUaDriver created:" << driver.name
            << "endpoint:" << m_endpoint
            << "tags:" << m_nodeIds.size();
}

OpcUaDriver::~OpcUaDriver()
{
    stop();
}

bool OpcUaDriver::start()
{
    m_stopped = false;
    m_client = UA_Client_new();
    UA_ClientConfig* config = UA_Client_getConfig(m_client);
    UA_ClientConfig_setDefault(config);
    config->timeout = 5000;

    connectToServer();                 // ✅ ناهمگرم
    m_iterateTimer.start(m_iterateMs);
    return true;
}


void OpcUaDriver::stop()
{
    m_stopped = true;
    m_iterateTimer.stop();
    m_reconnectTimer.stop();
    m_pollTimer.stop();
    if (m_client) {
        if (m_sessionActive) UA_Client_disconnect(m_client);
        UA_Client_delete(m_client);
        m_client = nullptr;
    }
    m_sessionActive = false;
    m_subscriptionReady = false;
    m_connectPending = false;
}
bool OpcUaDriver::isConnected() const
{
    return m_sessionActive;
}

void OpcUaDriver::connectToServer()
{
    if (!m_client || m_stopped) return;
    if (m_connectPending || m_sessionActive) return;

    const UA_StatusCode retval =
        UA_Client_connectAsync(m_client, m_endpoint.toUtf8().constData());  // ✅ غیرمسدود

    if (retval != UA_STATUSCODE_GOOD) {
        qWarning() << "OpcUaDriver: connectAsync failed:" << UA_StatusCode_name(retval);
        if (!m_reconnectTimer.isActive()) m_reconnectTimer.start(3000);
        return;
    }
    m_connectPending = true;
    qInfo() << "OpcUaDriver: async connect started:" << m_endpoint;
}

void OpcUaDriver::onReconnectTimer()
{
    if (m_stopped) return;
    connectToServer();
}
void OpcUaDriver::onIterateTimer()
{
    if (m_stopped || !m_client) return;

    UA_Client_run_iterate(m_client, 0);   // ✅ پیشبرد state machine بدون بلاک

    UA_SecureChannelState channelState;
    UA_SessionState sessionState;
    UA_StatusCode status;
    UA_Client_getState(m_client, &channelState, &sessionState, &status);

    if (sessionState == UA_SESSIONSTATE_ACTIVATED) {
        m_connectPending = false;
        if (!m_sessionActive) {
            m_sessionActive = true;
            qInfo() << "OpcUaDriver: session activated";
        }
        if (!m_subscriptionTried) {
            m_subscriptionTried = true;
            setupSubscription();
            if (m_usePolling) m_pollTimer.start();
        }
        return;
    }

    if (status != UA_STATUSCODE_GOOD || channelState == UA_SECURECHANNELSTATE_CLOSED) {
        if (m_sessionActive) {
            qWarning() << "OpcUaDriver: session lost";
            resetSession();
        }
        m_connectPending = false;
        if (!m_reconnectTimer.isActive()) m_reconnectTimer.start(3000);
    }
}
void OpcUaDriver::resetSession()
{
    m_sessionActive = false;
    m_subscriptionReady = false;
    m_subscriptionTried = false;
    m_usePolling = false;
    m_pollTimer.stop();
}

void OpcUaDriver::setupSubscription()
{
    UA_CreateSubscriptionRequest req = UA_CreateSubscriptionRequest_default();
    req.requestedPublishingInterval = 250.0;

    qInfo() << "OpcUaDriver: creating subscription...";

    UA_CreateSubscriptionResponse resp =
        UA_Client_Subscriptions_create(m_client, req, this, nullptr, nullptr);

    if (resp.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
        qWarning() << "OpcUaDriver: subscription failed:"
                   << UA_StatusCode_name(resp.responseHeader.serviceResult)
                   << "-> polling mode";
        m_usePolling = true;
        return;
    }

    m_subscriptionId = resp.subscriptionId;
    m_subscriptionReady = true;
    qInfo() << "OpcUaDriver: subscription created, id=" << m_subscriptionId;
    addMonitoredItems();
}

void OpcUaDriver::addMonitoredItems()
{
    for (auto it = m_nodeIds.constBegin(); it != m_nodeIds.constEnd(); ++it) {
        const qint64 tagId = it.key();
        UA_NodeId nodeId = parseNodeId(it.value());

        qInfo() << "OpcUaDriver: monitoring tag" << tagId
                << "node=" << it.value();

        UA_MonitoredItemCreateRequest monRequest = UA_MonitoredItemCreateRequest_default(nodeId);
        monRequest.requestedParameters.samplingInterval = 250.0;

        UA_MonitoredItemCreateResult result =
            UA_Client_MonitoredItems_createDataChange(
                m_client, m_subscriptionId, UA_TIMESTAMPSTORETURN_BOTH,
                monRequest, reinterpret_cast<void*>(tagId), onDataChange, nullptr);

        if (result.statusCode != UA_STATUSCODE_GOOD) {
            qWarning() << "OpcUaDriver: monitor failed for tag:" << tagId
                       << UA_StatusCode_name(result.statusCode);
        } else {
            qInfo() << "OpcUaDriver: monitor created for tag" << tagId;
        }

        UA_NodeId_clear(&nodeId);
    }
}

void OpcUaDriver::onDataChange(UA_Client*, UA_UInt32, void* subContext,
                               UA_UInt32, void* monContext, UA_DataValue* value)
{
    OpcUaDriver* self = static_cast<OpcUaDriver*>(subContext);
    if (!self) return;

    const qint64 tagId = reinterpret_cast<qint64>(monContext);

    // ✅ لاگ debug
//    qInfo() << "OpcUaDriver: data change for tag" << tagId;

    if (!value->hasValue) {
        qWarning() << "OpcUaDriver: tag" << tagId << "has no value";
        self->publishBad(tagId);
        return;
    }

    const double v = self->variantToDouble(value->value);
    if (std::isnan(v)) {
        qWarning() << "OpcUaDriver: tag" << tagId << "variant decode failed";
        self->publishBad(tagId);
    } else {
        qInfo() << "OpcUaDriver: tag" << tagId << "value=" << v;
        self->publishGood(tagId, v);
    }
}

void OpcUaDriver::onPollTimer()
{
    if (m_stopped || !m_client || !m_sessionActive) return;
    pollAllTags();
}

void OpcUaDriver::pollAllTags()
{
    for (auto it = m_nodeIds.constBegin(); it != m_nodeIds.constEnd(); ++it) {
        UA_NodeId nodeId = parseNodeId(it.value());

        UA_Variant value;
        UA_Variant_init(&value);
        const UA_StatusCode retval = UA_Client_readValueAttribute(m_client, nodeId, &value);


        if (retval == UA_STATUSCODE_GOOD && value.type != nullptr && value.data != nullptr) {
            const double v = variantToDouble(value);
            if (std::isnan(v))
                publishBad(it.key());
            else
                publishGood(it.key(), v);
        } else {
            publishBad(it.key());
        }

        UA_Variant_clear(&value);
        UA_NodeId_clear(&nodeId);
    }
}

// ============================================================
// Publish
// ============================================================

void OpcUaDriver::publishGood(qint64 tagId, double rawValue)
{
    const TagDefinition tag = m_tagMap.value(tagId);

    TagValue value;
    value.tagId = tagId;
    value.tagName = tag.tagName;
    value.timestamp = QDateTime::currentDateTimeUtc();
    value.rawValue = rawValue;
    value.engineeringValue = ScalingEngine::scale(tag, rawValue);
    value.quality = Quality::Good;
    value.source = SourceKind::RealDriver;

    m_lastValues[tagId] = value;
    m_bus.publish(QStringLiteral("tags/%1/raw").arg(tagId), value);
}

void OpcUaDriver::publishBad(qint64 tagId)
{
    TagValue value = m_lastValues.value(tagId);
    const TagDefinition tag = m_tagMap.value(tagId);

    value.tagId = tagId;
    value.tagName = tag.tagName;
    value.timestamp = QDateTime::currentDateTimeUtc();
    value.quality = Quality::Bad;
    value.source = SourceKind::RealDriver;

    m_lastValues[tagId] = value;
    m_bus.publish(QStringLiteral("tags/%1/raw").arg(tagId), value);
}

// ============================================================
// Helpers
// ============================================================

double OpcUaDriver::variantToDouble(const UA_Variant& v) const
{
    if (v.type == nullptr || v.data == nullptr)
        return std::numeric_limits<double>::quiet_NaN();

    const UA_DataType* t = v.type;
    if (t == &UA_TYPES[UA_TYPES_BOOLEAN]) return (*(UA_Boolean*)v.data) ? 1.0 : 0.0;
    if (t == &UA_TYPES[UA_TYPES_SBYTE])   return *(UA_SByte*)v.data;
    if (t == &UA_TYPES[UA_TYPES_BYTE])    return *(UA_Byte*)v.data;
    if (t == &UA_TYPES[UA_TYPES_INT16])   return *(UA_Int16*)v.data;
    if (t == &UA_TYPES[UA_TYPES_UINT16])  return *(UA_UInt16*)v.data;
    if (t == &UA_TYPES[UA_TYPES_INT32])   return *(UA_Int32*)v.data;
    if (t == &UA_TYPES[UA_TYPES_UINT32])  return *(UA_UInt32*)v.data;
    if (t == &UA_TYPES[UA_TYPES_INT64])   return (double)(*(UA_Int64*)v.data);
    if (t == &UA_TYPES[UA_TYPES_UINT64])  return (double)(*(UA_UInt64*)v.data);
    if (t == &UA_TYPES[UA_TYPES_FLOAT])   return *(UA_Float*)v.data;
    if (t == &UA_TYPES[UA_TYPES_DOUBLE])  return *(UA_Double*)v.data;

    return std::numeric_limits<double>::quiet_NaN();
}

UA_NodeId OpcUaDriver::parseNodeId(const QString& nodeIdStr) const
{
    UA_NodeId nodeId = UA_NODEID_NULL;

    const QStringList parts = nodeIdStr.split(';');
    UA_UInt16 ns = 0;
    QString idPart = nodeIdStr;

    for (const QString& p : parts) {
        if (p.startsWith("ns="))
            ns = p.mid(3).toUShort();
        else
            idPart = p;
    }

    if (idPart.startsWith("s=")) {
        const QByteArray s = idPart.mid(2).toUtf8();
        nodeId = UA_NODEID_STRING_ALLOC(ns, s.data());
    } else if (idPart.startsWith("i=")) {
        nodeId = UA_NODEID_NUMERIC(ns, idPart.mid(2).toUInt());
    }

    return nodeId;
}
