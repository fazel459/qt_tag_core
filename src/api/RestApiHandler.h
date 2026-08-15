#ifndef RESTAPIHANDLER_H
#define RESTAPIHANDLER_H

#include <QObject>
#include "HttpServer.h"
#include "ApiAuthenticator.h"
#include "../core/Models.h"
#include "DashboardManager.h"

class DbManager;
class DashboardManager;
class ReportGenerator;
class WebSocketHandler;
class UserManager;
class DriverManager;

class RestApiHandler : public QObject
{
    Q_OBJECT

public:
    explicit RestApiHandler(DbManager& db, DashboardManager* dashboardManager,
                               ReportGenerator* reportGenerator, QObject *parent = nullptr);


    HttpResponse handleRequest(const HttpRequest& request);
    void setAuthenticator(const ApiAuthenticator::Config& config);
    void setWebSocketHandler(WebSocketHandler* ws);

    void setUserManager(UserManager* um);
    void setDriverManager(DriverManager* dm);

private:
    DbManager& m_db;
    ApiAuthenticator m_auth;
    DashboardManager* m_dashboardManager;
    ReportGenerator* m_reportGenerator;
    WebSocketHandler* m_wsHandler = nullptr;

    UserManager* m_userManager = nullptr;


    // Tags
    HttpResponse handleGetTags(const HttpRequest& request);
    HttpResponse handleGetTag(const HttpRequest& request, qint64 tagId);
    HttpResponse handleCreateTag(const HttpRequest& request);
    HttpResponse handleUpdateTag(const HttpRequest& request, qint64 tagId);
    HttpResponse handleDeleteTag(const HttpRequest& request, qint64 tagId);
    HttpResponse handleGetTagCurrent(const HttpRequest& request, qint64 tagId);
    HttpResponse handleGetTagHistory(const HttpRequest& request, qint64 tagId);

    // Alarms
    HttpResponse handleGetAlarms(const HttpRequest& request);
    HttpResponse handleAckAlarm(const HttpRequest& request, qint64 alarmId);

    // Drivers
    HttpResponse handleGetDrivers(const HttpRequest& request);

    // System
    HttpResponse handleGetSystemStatus(const HttpRequest& request);
    HttpResponse handleHealthCheck(const HttpRequest& request);

    // Helpers
    bool parseTagIdFromPath(const QString& path, qint64& tagId, const QString& suffix = QString()) const;
    QJsonObject tagToJson(const TagDefinition& tag) const;
    bool jsonToTag(const QJsonObject& json, TagDefinition& tag, bool isUpdate) const;

    QJsonObject dashboardToJson(const DashboardDefinition& dashboard) const;
    bool jsonToDashboard(const QJsonObject& json, DashboardDefinition& dashboard, bool isUpdate) const;
    bool parseIdFromPath(const QString& path, qint64& id, const QString& prefix, const QString& suffix = QString()) const;

    // Dashboard
    HttpResponse handleGetDashboards(const HttpRequest& request);
    HttpResponse handleGetDashboard(const HttpRequest& request, qint64 dashboardId);
    HttpResponse handleCreateDashboard(const HttpRequest& request);
    HttpResponse handleUpdateDashboard(const HttpRequest& request, qint64 dashboardId);
    HttpResponse handleDeleteDashboard(const HttpRequest& request, qint64 dashboardId);

    // Handler های dashboard
    HttpResponse handleGetDashboardContent(const HttpRequest& request, qint64 dashboardId);
    HttpResponse handlePutDashboardContent(const HttpRequest& request, qint64 dashboardId);
    HttpResponse handleGetResources(const HttpRequest& request, qint64 dashboardId);

    // Handler های report
    HttpResponse handleTagHistoryReport(const HttpRequest& request);
    HttpResponse handleAlarmReport(const HttpRequest& request);
    HttpResponse handleDailySummaryReport(const HttpRequest& request);

    // Helper ها
    QVector<qint64> parseTagIdsParam(const QString& tagIdsStr) const;
    QDateTime parseDateTimeParam(const QString& str, const QDateTime& defaultValue) const;

    HttpResponse handleLogin(const HttpRequest& request);
    HttpResponse handleLogout(const HttpRequest& request);
    HttpResponse handleMe(const HttpRequest& request);
    HttpResponse handleGetUsers(const HttpRequest& request);
    HttpResponse handleCreateUser(const HttpRequest& request);
    HttpResponse handleUpdateUser(const HttpRequest& request, qint64 userId);
    HttpResponse handleDeleteUser(const HttpRequest& request, qint64 userId);
    HttpResponse handleUserChangePassword(const HttpRequest& request, qint64 userId);

    bool authenticateRequest(const HttpRequest& request, UserDefinition& user);
    QString extractBearerToken(const HttpRequest& request) const;

    HttpResponse handleCreateDriver(const HttpRequest& request);
    HttpResponse handleUpdateDriver(const HttpRequest& request, qint64 driverId);
    HttpResponse handleDeleteDriver(const HttpRequest& request, qint64 driverId);
    bool jsonToDriver(const QJsonObject& json, DriverDefinition& def, bool isUpdate, QString& error) const;
    void refreshDriverManagerConfig();
    DriverManager* m_driverManager = nullptr;

};

#endif // RESTAPIHANDLER_H
