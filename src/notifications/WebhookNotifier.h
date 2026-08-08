#ifndef WEBHOOKNOTIFIER_H
#define WEBHOOKNOTIFIER_H
#pragma once

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QObject>
#include <QUrl>

#include "../core/Models.h"

class WebhookNotifier : public QObject
{
public:
    explicit WebhookNotifier(QObject* parent = nullptr);

    void send(
        const AlarmNotification& alarm,
        const NotificationRule& rule
    );

private:
    void onReplyFinished(QNetworkReply* reply);

    QNetworkAccessManager m_networkManager;
};

#endif // WEBHOOKNOTIFIER_H
