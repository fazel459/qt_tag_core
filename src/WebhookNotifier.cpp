#include "WebhookNotifier.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

WebhookNotifier::WebhookNotifier(QObject* parent)
    : QObject(parent)
{
    QObject::connect(&m_networkManager, &QNetworkAccessManager::finished, this, &WebhookNotifier::onReplyFinished);
}

void WebhookNotifier::send(
    const AlarmNotification& alarm,
    const NotificationRule& rule
)
{
    QJsonObject configObj;

    const QJsonDocument configDoc = QJsonDocument::fromJson(rule.channelConfig.toUtf8());

    if (configDoc.isObject())
    {
        configObj = configDoc.object();
    }

    const QString url = configObj.value("url").toString();

    if (url.isEmpty())
    {
        qWarning() << "WebhookNotifier: url is empty for rule" << rule.name;
        return;
    }

    QJsonObject payload;

    payload["alarm_id"] = alarm.alarmId;
    payload["tag_id"] = alarm.tagId;
    payload["tag_name"] = alarm.tagName;
    payload["alarm_type"] = alarm.alarmType;
    payload["severity"] = alarm.severity;
    payload["state"] = alarm.state;
    payload["value"] = alarm.value;
    payload["threshold"] = alarm.threshold;
    payload["message"] = alarm.message;
    payload["timestamp"] = alarm.timestamp.toString(Qt::ISODateWithMs);

    const QJsonDocument payloadDoc(payload);
    const QByteArray payloadData = payloadDoc.toJson(QJsonDocument::Compact);

    QNetworkRequest request(QUrl(url));

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    const QString method = configObj.value("method").toString("POST").toUpper();

    QNetworkReply* reply = nullptr;

    if (method == "GET")
    {
        reply = m_networkManager.get(request);
    }
    else
    {
        reply = m_networkManager.post(request, payloadData);
    }

    if (reply != nullptr)
    {
        qInfo() << "WebhookNotifier: sent request to" << url
                << "for alarm" << alarm.alarmId;
    }
}

void WebhookNotifier::onReplyFinished(QNetworkReply* reply)
{
    if (reply == nullptr)
    {
        return;
    }

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString responseText = QString::fromUtf8(reply->readAll());

    if (reply->error() != QNetworkReply::NoError)
    {
        qWarning() << "WebhookNotifier: request failed:"
                   << reply->errorString()
                   << "status:" << statusCode;
    }
    else
    {
        qInfo() << "WebhookNotifier: request succeeded:"
                << "status:" << statusCode;
    }

    reply->deleteLater();
}
