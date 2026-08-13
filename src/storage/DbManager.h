#pragma once

#include <QSqlDatabase>
#include <QJsonObject>

#include "../core/Models.h"



class DbManager
{
public:
    bool initialize(const AppConfig& cfg);

    bool upsertTag(const TagDefinition& tag);

    bool insertRaw(const TagValue& value);
    bool upsertCurrent(const TagValue& value);

    bool insertAlarm(
            qint64 tagId,
            const QString& ruleType,
            const QString& severity,
            double value,
            double threshold,
            const QString& message
            );

    bool clearActiveAlarms(qint64 tagId, const QString& ruleType);
    bool writeBatch(const QVector<TagValue>& rawValues,const QVector<TagValue>& latestValues);

    bool insertRawBatch(const QVector<TagValue>& rawValues);
    bool upsertCurrentBatch(const QVector<TagValue>& latestValues);

    int countTags();
    int countRules();

    QVector<TagDefinition> loadTags();
    QVector<ThresholdRule> loadRules();

    bool insertRule(const ThresholdRule& rule);

    QString settingValue(const QString& key, const QString& defaultValue = QString()) const;
    bool setSetting(const QString& key, const QString& value);

    int settingInt(const QString& key, int defaultValue) const;
    double settingDouble(const QString& key, double defaultValue) const;

    QVector<DriverDefinition> loadDrivers();

    qint64 raiseAlarm(
        qint64 tagId,
        const QString& alarmType,
        const QString& severity,
        double value,
        double threshold,
        const QString& message
    );

    bool clearAlarmByTagAndType(qint64 tagId, const QString& alarmType);

    bool acknowledgeAlarm(qint64 alarmId, const QString& userName);

    bool addAlarmEvent(
        qint64 alarmId,
        const QString& eventType,
        const QString& eventData = QString(),
        const QString& userName = QString()
    );

    QVector<RangeViolationRule> loadRangeViolationRules();
    QVector<RateOfChangeRule> loadRateOfChangeRules();
    QVector<StuckValueRule> loadStuckValueRules();
    QVector<BooleanRule> loadBooleanRules();

    QVector<NotificationRule> loadNotificationRules();
    QVector<ComputedTag> loadComputedTags();


    bool logNotification(
        qint64 alarmId,
        qint64 notificationRuleId,
        const QString& channel,
        const QString& status,
        const QString& message
    );

    QJsonObject getTagCurrentState(int tagId);
    QVector<QJsonObject> getTagsCurrentState(const QVector<int>& tagIds);
    QVector<QJsonObject> loadAlarms(int limit = 100, int offset = 0);
    QSqlDatabase database() const;


    bool deleteTag(qint64 tagId);
    QVector<QJsonObject> queryTagHistory(
        qint64 tagId,
        const QDateTime& fromTime,
        const QDateTime& toTime,
        const QString& interval = QString(),
        int limit = 1000
    );

    QVector<DashboardDefinition> loadDashboards();
    DashboardDefinition loadDashboard(qint64 dashboardId);
    qint64 insertDashboard(const DashboardDefinition& dashboard);
    bool updateDashboard(const DashboardDefinition& dashboard);
    bool deleteDashboard(qint64 dashboardId);
    bool touchDashboard(qint64 dashboardId);

    int userCount();
    UserDefinition loadUserByUsername(const QString& username);
    UserDefinition loadUserById(qint64 userId);
    QVector<UserDefinition> loadUsers();
    qint64 insertUserRaw(const QString& username, const QString& passwordHash,
                         const QString& salt, const QString& displayName, const QString& role);
    bool updateUser(const UserDefinition& user);
    bool deleteUser(qint64 userId);
    bool updateUserPassword(qint64 userId, const QString& passwordHash, const QString& salt);
    bool touchLastLogin(qint64 userId);


private:
    bool migrate();

    static QString sourceToString(SourceKind source);
    bool migrateApiTables();   // ✅ جدول‌های لایه API
    void seedDefaults();       // ✅ داده‌های اولیه (idempotent)
    QSqlDatabase m_db;
};
