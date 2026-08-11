#include "WebSocketHandler.h"

#include <QWebSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QMetaObject>
#include <QAbstractSocket>
#include <QWebSocketProtocol>

WebSocketHandler::WebSocketHandler(QObject *parent)
    : QObject(parent)
{
    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout,
            this, &WebSocketHandler::heartbeat);
    m_heartbeatTimer->start(30000);

    m_batchTimer = new QTimer(this);
    m_batchTimer->setSingleShot(true);
    connect(m_batchTimer, &QTimer::timeout,
            this, &WebSocketHandler::flushPendingTagUpdates);
}

WebSocketHandler::~WebSocketHandler()
{
}

void WebSocketHandler::setBatchInterval(int msec)
{
    if (msec < 0) {
        msec = 0;
    }
    m_batchIntervalMs = msec;
}

void WebSocketHandler::addClient(QWebSocket *socket)
{
    if (!socket) {
        return;
    }

    socket->setParent(this);

    connect(socket, &QWebSocket::textMessageReceived,
            this, &WebSocketHandler::onTextMessage);

    connect(socket, &QWebSocket::disconnected,
            this, &WebSocketHandler::onDisconnected);

    connect(socket, &QWebSocket::pong,
            this, &WebSocketHandler::onPong);

    ClientInfo info;
    info.socket = socket;
    info.connectedAt = QDateTime::currentDateTimeUtc();
    info.lastPong = info.connectedAt;
    info.batchMode = false;

    m_clients.insert(socket, info);

    QUrl url = socket->requestUrl();
    routeInitialUrl(socket, url);

    sendHello(socket);

    emit clientCountChanged(m_clients.size());
}

void WebSocketHandler::routeInitialUrl(QWebSocket *socket, const QUrl &url)
{
    QString path = url.path();
    while (path.endsWith('/')) {
        path.chop(1);
    }

    QStringList parts = path.split('/', QString::SkipEmptyParts);
    if (parts.isEmpty() || parts.first() != QStringLiteral("ws")) {
        return;
    }

    if (parts.size() < 2) {
        return;
    }

    const QString root = parts.at(1).toLower();
    QUrlQuery query(url);

    if (root == QStringLiteral("alarms")) {
        if (parts.size() >= 3) {
            const QString type = parts.at(2).toLower();
            if (type == QStringLiteral("analog") ||
                type == QStringLiteral("digital") ||
                type == QStringLiteral("all")) {
                subscribeChannel(socket, QStringLiteral("alarms/") + type);
            }
        }
    } else if (root == QStringLiteral("system")) {
        if (parts.size() >= 3 && parts.at(2).toLower() == QStringLiteral("status")) {
            subscribeChannel(socket, QStringLiteral("system/status"));
        }
    } else if (root == QStringLiteral("tags")) {
        if (parts.size() >= 3 && parts.at(2).toLower() == QStringLiteral("batch")) {
            if (m_clients.contains(socket)) {
                m_clients[socket].batchMode = true;
            }

            const QString ids = query.queryItemValue(QStringLiteral("ids"), QUrl::FullyDecoded);
            const QList<int> tagIds = toIntList(QJsonValue(ids));

            for (int id : tagIds) {
                subscribeTag(socket, id);
            }
        }
    } else if (root == QStringLiteral("dashboard")) {
        if (parts.size() >= 3) {
            const QString dashboardId = parts.at(2);
            subscribeDashboard(socket, dashboardId);

            const QString ids = query.queryItemValue(QStringLiteral("ids"), QUrl::FullyDecoded);
            const QList<int> tagIds = toIntList(QJsonValue(ids));

            for (int id : tagIds) {
                subscribeTag(socket, id);
            }
        }
    }
}

void WebSocketHandler::sendHello(QWebSocket *socket)
{
    if (!socket || !m_clients.contains(socket)) {
        return;
    }

    const ClientInfo info = m_clients.value(socket);

    QJsonArray channels;
    for (const QString &channel : info.channels) {
        channels.append(channel);
    }

    QJsonArray tags;
    for (int tagId : info.tagIds) {
        tags.append(tagId);
    }

    QJsonArray dashboards;
    for (const QString &dashboard : info.dashboards) {
        dashboards.append(dashboard);
    }

    QJsonObject session;
    session.insert(QStringLiteral("channels"), channels);
    session.insert(QStringLiteral("tags"), tags);
    session.insert(QStringLiteral("dashboards"), dashboards);
    session.insert(QStringLiteral("delivery"),
                   info.batchMode ? QStringLiteral("batch") : QStringLiteral("event"));

    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("hello"));
    message.insert(QStringLiteral("server"), QStringLiteral("TagCore"));
    message.insert(QStringLiteral("ts"), currentUtcIso());
    message.insert(QStringLiteral("heartbeat_ms"), 30000);
    message.insert(QStringLiteral("batch_interval_ms"), m_batchIntervalMs);
    message.insert(QStringLiteral("session"), session);

    sendJson(socket, message);
}

void WebSocketHandler::sendJson(QWebSocket *socket, const QJsonObject &message)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    const QByteArray data = QJsonDocument(message).toJson(QJsonDocument::Compact);
    socket->sendTextMessage(QString::fromUtf8(data));
}

void WebSocketHandler::sendError(QWebSocket *socket, const QString &message)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("error"));
    obj.insert(QStringLiteral("message"), message);
    obj.insert(QStringLiteral("ts"), currentUtcIso());

    sendJson(socket, obj);
}

void WebSocketHandler::sendAck(QWebSocket *socket, const QJsonObject &request)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("ack"));
    obj.insert(QStringLiteral("op"), request.value(QStringLiteral("op")).toString());
    obj.insert(QStringLiteral("ts"), currentUtcIso());

    sendJson(socket, obj);
}

void WebSocketHandler::subscribeChannel(QWebSocket *socket, const QString &channel)
{
    if (!socket || !m_clients.contains(socket) || !isAllowedChannel(channel)) {
        return;
    }

    ClientInfo &info = m_clients[socket];
    if (!info.channels.contains(channel)) {
        info.channels.insert(channel);
        m_channelClients[channel].insert(socket);
    }
}

void WebSocketHandler::unsubscribeChannel(QWebSocket *socket, const QString &channel)
{
    if (!socket || !m_clients.contains(socket)) {
        return;
    }

    ClientInfo &info = m_clients[socket];
    if (info.channels.remove(channel)) {
        auto it = m_channelClients.find(channel);
        if (it != m_channelClients.end()) {
            it.value().remove(socket);
            if (it.value().isEmpty()) {
                m_channelClients.erase(it);
            }
        }
    }
}

void WebSocketHandler::subscribeTag(QWebSocket *socket, int tagId)
{
    if (!socket || !m_clients.contains(socket) || tagId < 0) {
        return;
    }

    ClientInfo &info = m_clients[socket];
    if (!info.tagIds.contains(tagId)) {
        info.tagIds.insert(tagId);
        m_tagClients[tagId].insert(socket);
    }
}

void WebSocketHandler::unsubscribeTag(QWebSocket *socket, int tagId)
{
    if (!socket || !m_clients.contains(socket)) {
        return;
    }

    ClientInfo &info = m_clients[socket];
    if (info.tagIds.remove(tagId)) {
        auto it = m_tagClients.find(tagId);
        if (it != m_tagClients.end()) {
            it.value().remove(socket);
            if (it.value().isEmpty()) {
                m_tagClients.erase(it);
            }
        }
    }
}

void WebSocketHandler::subscribeDashboard(QWebSocket *socket, const QString &dashboardId)
{
    if (!socket || !m_clients.contains(socket) || dashboardId.isEmpty()) {
        return;
    }

    ClientInfo &info = m_clients[socket];
    if (!info.dashboards.contains(dashboardId)) {
        info.dashboards.insert(dashboardId);
        m_dashboardClients[dashboardId].insert(socket);
    }
}

void WebSocketHandler::unsubscribeDashboard(QWebSocket *socket, const QString &dashboardId)
{
    if (!socket || !m_clients.contains(socket)) {
        return;
    }

    ClientInfo &info = m_clients[socket];
    if (info.dashboards.remove(dashboardId)) {
        auto it = m_dashboardClients.find(dashboardId);
        if (it != m_dashboardClients.end()) {
            it.value().remove(socket);
            if (it.value().isEmpty()) {
                m_dashboardClients.erase(it);
            }
        }
    }
}

void WebSocketHandler::removeClient(QWebSocket *socket)
{
    if (!socket || !m_clients.contains(socket)) {
        return;
    }

    const ClientInfo info = m_clients.take(socket);

    for (const QString &channel : info.channels) {
        auto it = m_channelClients.find(channel);
        if (it != m_channelClients.end()) {
            it.value().remove(socket);
            if (it.value().isEmpty()) {
                m_channelClients.erase(it);
            }
        }
    }

    for (int tagId : info.tagIds) {
        auto it = m_tagClients.find(tagId);
        if (it != m_tagClients.end()) {
            it.value().remove(socket);
            if (it.value().isEmpty()) {
                m_tagClients.erase(it);
            }
        }
    }

    for (const QString &dashboardId : info.dashboards) {
        auto it = m_dashboardClients.find(dashboardId);
        if (it != m_dashboardClients.end()) {
            it.value().remove(socket);
            if (it.value().isEmpty()) {
                m_dashboardClients.erase(it);
            }
        }
    }

    socket->deleteLater();

    emit clientCountChanged(m_clients.size());
}

void WebSocketHandler::onTextMessage(const QString &message)
{
    QWebSocket *socket = qobject_cast<QWebSocket*>(sender());
    if (!socket) {
        return;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);

    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        sendError(socket, QStringLiteral("Invalid JSON message"));
        return;
    }

    handleControlMessage(socket, doc.object());
}

void WebSocketHandler::handleControlMessage(QWebSocket *socket, const QJsonObject &obj)
{
    const QString op = obj.value(QStringLiteral("op")).toString().toLower();

    if (op == QStringLiteral("ping")) {
        QJsonObject message;
        message.insert(QStringLiteral("type"), QStringLiteral("pong"));
        message.insert(QStringLiteral("ts"), currentUtcIso());
        sendJson(socket, message);
        return;
    }

    if (op == QStringLiteral("options")) {
        if (obj.contains(QStringLiteral("delivery")) && m_clients.contains(socket)) {
            const QString delivery = obj.value(QStringLiteral("delivery")).toString().toLower();
            m_clients[socket].batchMode = (delivery == QStringLiteral("batch"));
        }

        sendAck(socket, obj);
        return;
    }

    if (op == QStringLiteral("subscribe") || op == QStringLiteral("unsubscribe")) {
        const bool add = (op == QStringLiteral("subscribe"));

        if (obj.contains(QStringLiteral("channel"))) {
            const QStringList channels = toStringList(obj.value(QStringLiteral("channel")));
            for (const QString &channel : channels) {
                if (add) {
                    subscribeChannel(socket, channel);
                } else {
                    unsubscribeChannel(socket, channel);
                }
            }
        }

        if (obj.contains(QStringLiteral("tags"))) {
            const QList<int> tagIds = toIntList(obj.value(QStringLiteral("tags")));
            for (int tagId : tagIds) {
                if (add) {
                    subscribeTag(socket, tagId);
                } else {
                    unsubscribeTag(socket, tagId);
                }
            }
        }

        if (obj.contains(QStringLiteral("dashboard"))) {
            const QStringList dashboards = toStringList(obj.value(QStringLiteral("dashboard")));
            for (const QString &dashboardId : dashboards) {
                if (add) {
                    subscribeDashboard(socket, dashboardId);
                } else {
                    unsubscribeDashboard(socket, dashboardId);
                }
            }
        }

        if (obj.contains(QStringLiteral("delivery")) && m_clients.contains(socket)) {
            const QString delivery = obj.value(QStringLiteral("delivery")).toString().toLower();
            m_clients[socket].batchMode = (delivery == QStringLiteral("batch"));
        }

        sendAck(socket, obj);
        return;
    }

    sendError(socket, QStringLiteral("Unsupported op: %1").arg(op));
}

void WebSocketHandler::onDisconnected()
{
    QWebSocket *socket = qobject_cast<QWebSocket*>(sender());
    if (socket) {
        removeClient(socket);
    }
}

void WebSocketHandler::onPong(quint64 elapsedTime, const QByteArray &payload)
{
    Q_UNUSED(elapsedTime)
    Q_UNUSED(payload)

    QWebSocket *socket = qobject_cast<QWebSocket*>(sender());
    if (socket && m_clients.contains(socket)) {
        m_clients[socket].lastPong = QDateTime::currentDateTimeUtc();
    }
}

void WebSocketHandler::heartbeat()
{
    const QByteArray payload = QByteArray::number(QDateTime::currentMSecsSinceEpoch());
    const QDateTime now = QDateTime::currentDateTimeUtc();

    QList<QWebSocket*> staleSockets;
    const QList<QWebSocket*> sockets = m_clients.keys();

    for (QWebSocket *socket : sockets) {
        if (!socket) {
            continue;
        }

        const ClientInfo info = m_clients.value(socket);

        if (info.lastPong.isValid() && info.lastPong.secsTo(now) > 90) {
            staleSockets.append(socket);
        } else if (socket->state() == QAbstractSocket::ConnectedState) {
            socket->ping(payload);
        }
    }

    for (QWebSocket *socket : staleSockets) {
        if (socket) {
            socket->close(QWebSocketProtocol::CloseCodeGoingAway,
                          QStringLiteral("pong timeout"));
        }
    }
}

void WebSocketHandler::publishTagUpdate(int tagId, const QJsonObject &payload)
{
    QJsonObject message = payload;

    message.insert(QStringLiteral("type"), QStringLiteral("tag.update"));
    message.insert(QStringLiteral("tag_id"), tagId);

    if (!message.contains(QStringLiteral("ts"))) {
        message.insert(QStringLiteral("ts"), currentUtcIso());
    }

    QMetaObject::invokeMethod(this, [this, tagId, message]() {
        sendTagUpdateToEventClients(tagId, message);
        queueTagUpdate(tagId, message);
    }, Qt::AutoConnection);
}

void WebSocketHandler::publishAlarmEvent(const QJsonObject &alarm)
{
    QJsonObject message = alarm;

    if (!message.contains(QStringLiteral("type"))) {
        message.insert(QStringLiteral("type"), QStringLiteral("alarm.event"));
    }

    if (!message.contains(QStringLiteral("ts"))) {
        message.insert(QStringLiteral("ts"), currentUtcIso());
    }

    const QString category = alarmCategory(message);
    message.insert(QStringLiteral("category"), category);

    QMetaObject::invokeMethod(this, [this, message, category]() {
        broadcastToChannel(QStringLiteral("alarms/") + category, message);
        broadcastToChannel(QStringLiteral("alarms/all"), message);
    }, Qt::AutoConnection);
}

void WebSocketHandler::publishSystemStatus(const QJsonObject &status)
{
    QJsonObject message = status;

    if (!message.contains(QStringLiteral("type"))) {
        message.insert(QStringLiteral("type"), QStringLiteral("system.status"));
    }

    if (!message.contains(QStringLiteral("ts"))) {
        message.insert(QStringLiteral("ts"), currentUtcIso());
    }

    QMetaObject::invokeMethod(this, [this, message]() {
        broadcastToChannel(QStringLiteral("system/status"), message);
    }, Qt::AutoConnection);
}

void WebSocketHandler::publishDashboardEvent(const QString &dashboardId,
                                             const QJsonObject &payload)
{
    QJsonObject message = payload;

    if (!message.contains(QStringLiteral("type"))) {
        message.insert(QStringLiteral("type"), QStringLiteral("dashboard.event"));
    }

    message.insert(QStringLiteral("dashboard_id"), dashboardId);

    if (!message.contains(QStringLiteral("ts"))) {
        message.insert(QStringLiteral("ts"), currentUtcIso());
    }

    QMetaObject::invokeMethod(this, [this, dashboardId, message]() {
        broadcastToDashboard(dashboardId, message);
    }, Qt::AutoConnection);
}

// TagValue-based overloads
void WebSocketHandler::publishTagUpdate(const TagValue &value)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("value"), value.engineeringValue);
    payload.insert(QStringLiteral("raw_value"), value.rawValue);
    payload.insert(QStringLiteral("quality"), qualityToString(value.quality));
    payload.insert(QStringLiteral("source"), QString::number(int(value.source)));
    payload.insert(QStringLiteral("sequence"), int(value.sequence));

    if (value.timestamp.isValid()) {
        payload.insert(QStringLiteral("ts"),
                       value.timestamp.toString(Qt::ISODateWithMs));
    }

    publishTagUpdate(int(value.tagId), payload);
}

void WebSocketHandler::publishAlarmEvent(const TagValue &value)
{
    QJsonObject alarm;
    alarm.insert(QStringLiteral("tag_id"), int(value.tagId));
    alarm.insert(QStringLiteral("value"), value.engineeringValue);
    alarm.insert(QStringLiteral("quality"), qualityToString(value.quality));

    publishAlarmEvent(alarm);
}

void WebSocketHandler::publishSystemStatus(const TagValue &value)
{
    QJsonObject status;
    status.insert(QStringLiteral("message"), value.tagName);

    publishSystemStatus(status);
}

QString WebSocketHandler::qualityToString(Quality quality)
{
    switch (quality) {
    case Quality::Good:
        return QStringLiteral("good");
    case Quality::Bad:
        return QStringLiteral("bad");
    case Quality::Uncertain:
        return QStringLiteral("uncertain");
    default:
        return QStringLiteral("unknown");
    }
}

void WebSocketHandler::broadcastToChannel(const QString &channel,
                                          const QJsonObject &message)
{
    auto it = m_channelClients.constFind(channel);
    if (it == m_channelClients.constEnd()) {
        return;
    }

    const QSet<QWebSocket*> sockets = it.value();

    for (QWebSocket *socket : sockets) {
        if (m_clients.contains(socket)) {
            sendJson(socket, message);
        }
    }
}

void WebSocketHandler::broadcastToDashboard(const QString &dashboardId,
                                            const QJsonObject &message)
{
    auto it = m_dashboardClients.constFind(dashboardId);
    if (it == m_dashboardClients.constEnd()) {
        return;
    }

    const QSet<QWebSocket*> sockets = it.value();

    for (QWebSocket *socket : sockets) {
        if (m_clients.contains(socket)) {
            sendJson(socket, message);
        }
    }
}

void WebSocketHandler::sendTagUpdateToEventClients(int tagId,
                                                   const QJsonObject &message)
{
    auto it = m_tagClients.constFind(tagId);
    if (it == m_tagClients.constEnd()) {
        return;
    }

    const QSet<QWebSocket*> sockets = it.value();

    for (QWebSocket *socket : sockets) {
        if (!m_clients.contains(socket)) {
            continue;
        }

        const ClientInfo info = m_clients.value(socket);

        if (info.batchMode) {
            continue;
        }

        sendJson(socket, message);
    }
}

void WebSocketHandler::queueTagUpdate(int tagId, const QJsonObject &message)
{
    m_pendingTagUpdates[tagId] = message;

    if (m_batchIntervalMs <= 0) {
        flushPendingTagUpdates();
    } else if (m_batchTimer && !m_batchTimer->isActive()) {
        m_batchTimer->start(m_batchIntervalMs);
    }
}

void WebSocketHandler::flushPendingTagUpdates()
{
    if (m_batchTimer) {
        m_batchTimer->stop();
    }

    if (m_pendingTagUpdates.isEmpty()) {
        return;
    }

    QHash<int, QJsonObject> pending;
    pending.swap(m_pendingTagUpdates);

    const QList<QWebSocket*> sockets = m_clients.keys();

    for (QWebSocket *socket : sockets) {
        if (!socket || !m_clients.contains(socket)) {
            continue;
        }

        const ClientInfo info = m_clients.value(socket);

        if (!info.batchMode || info.tagIds.isEmpty()) {
            continue;
        }

        QJsonArray updates;

        for (auto it = pending.constBegin(); it != pending.constEnd(); ++it) {
            const int tagId = it.key();
            if (info.tagIds.contains(tagId)) {
                updates.append(it.value());
            }
        }

        if (!updates.isEmpty()) {
            QJsonObject message;
            message.insert(QStringLiteral("type"), QStringLiteral("tags.batch"));
            message.insert(QStringLiteral("ts"), currentUtcIso());
            message.insert(QStringLiteral("updates"), updates);

            sendJson(socket, message);
        }
    }
}

QStringList WebSocketHandler::toStringList(const QJsonValue &value) const
{
    QStringList result;

    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &v : array) {
            if (v.isString()) {
                result << v.toString();
            } else if (v.isDouble()) {
                result << QString::number(v.toDouble());
            }
        }
    } else if (value.isString()) {
        result = value.toString().split(',', QString::SkipEmptyParts);
        for (QString &item : result) {
            item = item.trimmed();
        }
    } else if (value.isDouble()) {
        result << QString::number(value.toDouble());
    }

    return result;
}

QList<int> WebSocketHandler::toIntList(const QJsonValue &value) const
{
    QList<int> result;

    if (value.isArray()) {
        const QJsonArray array = value.toArray();

        for (const QJsonValue &v : array) {
            bool ok = false;
            int id = 0;

            if (v.isDouble()) {
                id = int(v.toDouble());
                ok = true;
            } else if (v.isString()) {
                id = v.toString().trimmed().toInt(&ok);
            }

            if (ok && id >= 0) {
                result.append(id);
            }
        }
    } else if (value.isString()) {
        const QStringList parts = value.toString().split(',', QString::SkipEmptyParts);

        for (const QString &part : parts) {
            bool ok = false;
            const int id = part.trimmed().toInt(&ok);

            if (ok && id >= 0) {
                result.append(id);
            }
        }
    } else if (value.isDouble()) {
        result.append(int(value.toDouble()));
    }

    return result;
}

bool WebSocketHandler::isAllowedChannel(const QString &channel) const
{
    return channel == QStringLiteral("alarms/analog") ||
           channel == QStringLiteral("alarms/digital") ||
           channel == QStringLiteral("alarms/all") ||
           channel == QStringLiteral("system/status");
}

QString WebSocketHandler::alarmCategory(const QJsonObject &alarm) const
{
    if (alarm.contains(QStringLiteral("category")) &&
        alarm.value(QStringLiteral("category")).isString()) {

        const QString category = alarm.value(QStringLiteral("category")).toString().toLower();

        if (category == QStringLiteral("analog") || category == QStringLiteral("digital")) {
            return category;
        }
    }

    return QStringLiteral("analog");
}

QString WebSocketHandler::currentUtcIso()
{
    return QDateTime::currentDateTimeUtc()
        .toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz'Z'"));
}
