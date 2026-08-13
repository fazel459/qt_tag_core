#ifndef WEBSOCKETHANDLER_H
#define WEBSOCKETHANDLER_H

#include <QObject>
#include <QString>
#include <QHash>
#include <QSet>
#include <QList>
#include <QDateTime>
#include <QJsonObject>
#include <functional>
#include "../core/Models.h"

class QWebSocket;
class QTimer;
class QUrl;
class QJsonValue;

class WebSocketHandler : public QObject
{
    Q_OBJECT

public:
    explicit WebSocketHandler(QObject *parent = nullptr);
    ~WebSocketHandler() override;

    void addClient(QWebSocket *socket);
    void setBatchInterval(int msec);

    void publishTagUpdate(int tagId, const QJsonObject &payload);
    void publishAlarmEvent(const QJsonObject &alarm);
    void publishSystemStatus(const QJsonObject &status);
    void publishDashboardEvent(const QString &dashboardId, const QJsonObject &payload);

    // TagValue-based overloads
    void publishTagUpdate(const TagValue &value);
    void publishAlarmEvent(const TagValue &value);
    void publishSystemStatus(const TagValue &value);
    using SnapshotProvider = std::function<QVector<QJsonObject>(const QVector<int>& tagIds)>;
    void setSnapshotProvider(SnapshotProvider provider);
    void sendSnapshotToClient(QWebSocket *socket);

    using CommandHandler = std::function<QJsonObject(const QString& op, const QJsonObject& payload)>;
    void setCommandHandler(CommandHandler handler);

    void publishAlarmAck(qint64 alarmId, qint64 tagId, const QString& userName);

signals:
    void clientCountChanged(int count);

private slots:
    void onTextMessage(const QString &message);
    void onDisconnected();
    void onPong(quint64 elapsedTime, const QByteArray &payload);
    void heartbeat();
    void flushPendingTagUpdates();

private:
    struct ClientInfo {
        QWebSocket *socket = nullptr;
        QSet<QString> channels;
        QSet<int> tagIds;
        QSet<QString> dashboards;
        QDateTime connectedAt;
        QDateTime lastPong;
        bool batchMode = false;
    };

    QHash<QWebSocket*, ClientInfo> m_clients;
    QHash<QString, QSet<QWebSocket*>> m_channelClients;
    QHash<int, QSet<QWebSocket*>> m_tagClients;
    QHash<QString, QSet<QWebSocket*>> m_dashboardClients;
    QHash<int, QJsonObject> m_pendingTagUpdates;

    QTimer *m_heartbeatTimer = nullptr;
    QTimer *m_batchTimer = nullptr;
    int m_batchIntervalMs = 100;

    void sendJson(QWebSocket *socket, const QJsonObject &message);
    void sendError(QWebSocket *socket, const QString &message);
    void sendAck(QWebSocket *socket, const QJsonObject &request);
    void sendHello(QWebSocket *socket);

    void routeInitialUrl(QWebSocket *socket, const QUrl &url);
    void handleControlMessage(QWebSocket *socket, const QJsonObject &message);

    void subscribeChannel(QWebSocket *socket, const QString &channel);
    void unsubscribeChannel(QWebSocket *socket, const QString &channel);
    void subscribeTag(QWebSocket *socket, int tagId);
    void unsubscribeTag(QWebSocket *socket, int tagId);
    void subscribeDashboard(QWebSocket *socket, const QString &dashboardId);
    void unsubscribeDashboard(QWebSocket *socket, const QString &dashboardId);

    void removeClient(QWebSocket *socket);

    QStringList toStringList(const QJsonValue &value) const;
    QList<int> toIntList(const QJsonValue &value) const;
    bool isAllowedChannel(const QString &channel) const;
    QString alarmCategory(const QJsonObject &alarm) const;

    static QString currentUtcIso();

    void broadcastToChannel(const QString &channel, const QJsonObject &message);
    void broadcastToDashboard(const QString &dashboardId, const QJsonObject &message);
    void sendTagUpdateToEventClients(int tagId, const QJsonObject &message);
    void queueTagUpdate(int tagId, const QJsonObject &message);

    static QString qualityToString(Quality quality);

    SnapshotProvider m_snapshotProvider;

    void sendSnapshotForTags(QWebSocket *socket, const QVector<int>& tagIds);

    CommandHandler m_commandHandler;
    void handleCommandMessage(QWebSocket* socket, const QJsonObject& obj);
    void sendCommandResponse(QWebSocket* socket, const QString& id,
                             bool ok,
                             const QJsonObject& data = QJsonObject(),
                             const QString& error = QString());

};

#endif // WEBSOCKETHANDLER_H
