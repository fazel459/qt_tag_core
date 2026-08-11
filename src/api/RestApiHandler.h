#ifndef RESTAPIHANDLER_H
#define RESTAPIHANDLER_H

#include <QObject>
#include "HttpServer.h"
#include "ApiAuthenticator.h"
#include "../core/Models.h"

class DbManager;

class RestApiHandler : public QObject
{
    Q_OBJECT

public:
    explicit RestApiHandler(DbManager& db, QObject *parent = nullptr);

    HttpResponse handleRequest(const HttpRequest& request);
    void setAuthenticator(const ApiAuthenticator::Config& config);

private:
    DbManager& m_db;
    ApiAuthenticator m_auth;

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
};

#endif // RESTAPIHANDLER_H
