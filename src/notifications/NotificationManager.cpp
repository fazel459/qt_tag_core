#include "NotificationManager.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

NotificationManager::NotificationManager(
    TagBus& bus,
    DbManager& db,
    const AppConfig& config,
    QObject* parent
)
    : QObject(parent)
    , m_bus(bus)
    , m_db(db)
    , m_config(config)
    , m_webhookNotifier(new WebhookNotifier(this))
{
    m_bus.subscribe("alarms/#", [this](const BusMessage& message)
    {
        if (!message.topic.endsWith("/raised") && !message.topic.endsWith("/cleared"))
        {
            return;
        }

        AlarmNotification alarm;

        alarm.tagId = message.value.tagId;
        alarm.tagName = message.value.tagName;
        alarm.timestamp = message.value.timestamp;
        alarm.state = message.topic.endsWith("/raised") ? "raised" : "cleared";

        const QJsonObject metaObj;

        if (!message.value.tagName.isEmpty())
        {
            alarm.message = message.value.tagName;
        }

        onAlarmEvent(alarm);
    });

    qInfo() << "NotificationManager started:"
            << "notificationRules=" << config.notificationRules.size();
}

void NotificationManager::onAlarmEvent(const AlarmNotification& alarm)
{
    for (const NotificationRule& rule : m_config.notificationRules)
    {
        if (!rule.enabled)
        {
            continue;
        }

        if (!matchesSeverity(rule, alarm.severity))
        {
            continue;
        }

        if (!matchesAlarmType(rule, alarm.alarmType))
        {
            continue;
        }

        if (isThrottled(rule.notificationRuleId))
        {
            continue;
        }

        if (rule.channel == "webhook")
        {
            m_webhookNotifier->send(alarm, rule);
        }
        else if (rule.channel == "log")
        {
            qInfo() << "NOTIFICATION LOG:"
                    << "alarmId=" << alarm.alarmId
                    << "tag=" << alarm.tagName
                    << "type=" << alarm.alarmType
                    << "severity=" << alarm.severity
                    << "state=" << alarm.state
                    << "message=" << alarm.message;
        }
        else if (rule.channel == "sound")
        {
            qInfo() << "NOTIFICATION SOUND:"
                    << "alarmId=" << alarm.alarmId
                    << "severity=" << alarm.severity;
        }

        m_db.logNotification(
            alarm.alarmId,
            rule.notificationRuleId,
            rule.channel,
            "sent",
            alarm.message
        );

        updateThrottle(rule.notificationRuleId);
    }
}

bool NotificationManager::matchesSeverity(const NotificationRule& rule, const QString& severity) const
{
    if (rule.severityFilter.isEmpty())
    {
        return true;
    }

    const QStringList allowedSeverities = rule.severityFilter.split(',', QString::SkipEmptyParts);

    for (const QString& allowed : allowedSeverities)
    {
        if (allowed.trimmed().toLower() == severity.trimmed().toLower())
        {
            return true;
        }
    }

    return false;
}

bool NotificationManager::matchesAlarmType(const NotificationRule& rule, const QString& alarmType) const
{
    if (rule.alarmTypeFilter.isEmpty())
    {
        return true;
    }

    const QStringList allowedTypes = rule.alarmTypeFilter.split(',', QString::SkipEmptyParts);

    for (const QString& allowed : allowedTypes)
    {
        if (allowed.trimmed().toLower() == alarmType.trimmed().toLower())
        {
            return true;
        }
    }

    return false;
}

bool NotificationManager::isThrottled(qint64 notificationRuleId) const
{
    const QDateTime lastTime = m_lastNotificationTime.value(notificationRuleId);

    if (!lastTime.isValid())
    {
        return false;
    }

    const NotificationRule* rulePtr = nullptr;

    for (const NotificationRule& rule : m_config.notificationRules)
    {
        if (rule.notificationRuleId == notificationRuleId)
        {
            rulePtr = &rule;
            break;
        }
    }

    if (rulePtr == nullptr)
    {
        return false;
    }

    return lastTime.msecsTo(QDateTime::currentDateTimeUtc()) < rulePtr->throttleMs;
}

void NotificationManager::updateThrottle(qint64 notificationRuleId)
{
    m_lastNotificationTime[notificationRuleId] = QDateTime::currentDateTimeUtc();
}
