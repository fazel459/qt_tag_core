#ifndef NOTIFICATIONMANAGER_H
#define NOTIFICATIONMANAGER_H
#pragma once

#include <QDateTime>
#include <QHash>

#include "../core/Models.h"
#include "../storage/DbManager.h"
#include "../tagbus/TagBus.h"

#include "WebhookNotifier.h"

class NotificationManager : public QObject
{
public:
    NotificationManager(
        TagBus& bus,
        DbManager& db,
        const AppConfig& config,
        QObject* parent = nullptr
    );

private:
    void onAlarmEvent(const AlarmNotification& alarm);

    bool matchesSeverity(const NotificationRule& rule, const QString& severity) const;
    bool matchesAlarmType(const NotificationRule& rule, const QString& alarmType) const;
    bool isThrottled(qint64 notificationRuleId) const;
    void updateThrottle(qint64 notificationRuleId);

    TagBus& m_bus;
    DbManager& m_db;

    AppConfig m_config;

    WebhookNotifier* m_webhookNotifier;

    QHash<qint64, QDateTime> m_lastNotificationTime;
};
#endif // NOTIFICATIONMANAGER_H
