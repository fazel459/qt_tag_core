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

class RestApiHandler : public QObject
{
    Q_OBJECT

public:
    explicit RestApiHandler(DbManager& db, DashboardManager* dashboardManager,
                               ReportGenerator* reportGenerator, QObject *parent = nullptr);


    HttpResponse handleRequest(const HttpRequest& request);
    void setAuthenticator(const ApiAuthenticator::Config& config);

private:
    DbManager& m_db;
    ApiAuthenticator m_auth;
    DashboardManager* m_dashboardManager;
    ReportGenerator* m_reportGenerator;

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


};

#endif // RESTAPIHANDLER_H
