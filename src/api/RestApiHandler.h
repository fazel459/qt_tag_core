#ifndef RESTAPIHANDLER_H
#define RESTAPIHANDLER_H

#include <QObject>
#include "HttpServer.h"

class DbManager;

class RestApiHandler : public QObject
{
    Q_OBJECT

public:
    explicit RestApiHandler(DbManager& db, QObject *parent = nullptr);

    HttpResponse handleRequest(const HttpRequest& request);

private:
    DbManager& m_db;

    HttpResponse handleGetTags(const HttpRequest& request);
    HttpResponse handleGetTag(const HttpRequest& request, int tagId);
    HttpResponse handleCreateTag(const HttpRequest& request);
    HttpResponse handleUpdateTag(const HttpRequest& request, int tagId);
    HttpResponse handleDeleteTag(const HttpRequest& request, int tagId);
    HttpResponse handleGetTagCurrent(const HttpRequest& request, int tagId);
    HttpResponse handleGetAlarms(const HttpRequest& request);
    HttpResponse handleAckAlarm(const HttpRequest& request, qint64 alarmId);
    HttpResponse handleGetDrivers(const HttpRequest& request);
    HttpResponse handleGetSystemStatus(const HttpRequest& request);
};

#endif // RESTAPIHANDLER_H
