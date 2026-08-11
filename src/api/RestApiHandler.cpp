#include "RestApiHandler.h"
#include "../storage/DbManager.h"
#include "../core/Models.h"

#include <QJsonArray>
#include <QJsonDocument>

RestApiHandler::RestApiHandler(DbManager& db, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
}

HttpResponse RestApiHandler::handleRequest(const HttpRequest& request)
{
    QString path = request.path;

    if (request.method == "OPTIONS") {
        return HttpResponse::ok();
    }

    if (path == "/api/v1/tags") {
        if (request.method == "GET") {
            return handleGetTags(request);
        } else if (request.method == "POST") {
            return handleCreateTag(request);
        }
        return HttpResponse::badRequest("Method not allowed");
    }

    if (path.startsWith("/api/v1/tags/")) {
        if (path.endsWith("/current")) {
            QString idStr = path.mid(13, path.length() - 13 - 8);
            int tagId = idStr.toInt();
            if (request.method == "GET") {
                return handleGetTagCurrent(request, tagId);
            }
            return HttpResponse::badRequest("Method not allowed");
        }

        if (path.endsWith("/history")) {
            return HttpResponse::badRequest("History endpoint not implemented yet");
        }

        QString idStr = path.mid(13);
        int tagId = idStr.toInt();

        if (request.method == "GET") {
            return handleGetTag(request, tagId);
        } else if (request.method == "PUT") {
            return handleUpdateTag(request, tagId);
        } else if (request.method == "DELETE") {
            return handleDeleteTag(request, tagId);
        }
        return HttpResponse::badRequest("Method not allowed");
    }

    if (path == "/api/v1/alarms") {
        if (request.method == "GET") {
            return handleGetAlarms(request);
        }
        return HttpResponse::badRequest("Method not allowed");
    }

    if (path.startsWith("/api/v1/alarms/") && path.endsWith("/ack")) {
        QString idStr = path.mid(15, path.length() - 15 - 4);
        qint64 alarmId = idStr.toLongLong();
        if (request.method == "POST") {
            return handleAckAlarm(request, alarmId);
        }
        return HttpResponse::badRequest("Method not allowed");
    }

    if (path == "/api/v1/drivers") {
        if (request.method == "GET") {
            return handleGetDrivers(request);
        }
        return HttpResponse::badRequest("Method not allowed");
    }

    if (path == "/api/v1/system/status") {
        if (request.method == "GET") {
            return handleGetSystemStatus(request);
        }
        return HttpResponse::badRequest("Method not allowed");
    }

    return HttpResponse::notFound("Endpoint not found: " + path);
}

HttpResponse RestApiHandler::handleGetTags(const HttpRequest& request)
{
    Q_UNUSED(request)

    QJsonObject result;
    QJsonArray tags;

    QVector<TagDefinition> tagList = m_db.loadTags();

    for (const TagDefinition& tag : tagList) {
        QJsonObject tagObj;
        tagObj.insert("tag_id", tag.tagId);
        tagObj.insert("tag_name", tag.tagName);
        tagObj.insert("source_type", tag.sourceType);
        tagObj.insert("data_type", tag.dataType);
        tagObj.insert("eng_units", tag.engUnits);
        tagObj.insert("raw_min", tag.rawMin);
        tagObj.insert("raw_max", tag.rawMax);
        tagObj.insert("eng_min", tag.engMin);
        tagObj.insert("eng_max", tag.engMax);
        tagObj.insert("scaling_type", tag.scalingType);
        tagObj.insert("slope", tag.slope);
        tagObj.insert("offset", tag.offset);
        tagObj.insert("deadband", tag.deadband);
        tagObj.insert("enabled", tag.enabled);
        tagObj.insert("driver_id", tag.driverId);

        tags.append(tagObj);
    }

    result.insert("tags", tags);
    result.insert("count", tags.size());

    return HttpResponse::ok(result);
}

HttpResponse RestApiHandler::handleGetTag(const HttpRequest& request, int tagId)
{
    Q_UNUSED(request)

    QVector<TagDefinition> tagList = m_db.loadTags();

    for (const TagDefinition& tag : tagList) {
        if (tag.tagId == tagId) {
            QJsonObject tagObj;
            tagObj.insert("tag_id", tag.tagId);
            tagObj.insert("tag_name", tag.tagName);
            tagObj.insert("source_type", tag.sourceType);
            tagObj.insert("data_type", tag.dataType);
            tagObj.insert("eng_units", tag.engUnits);
            tagObj.insert("raw_min", tag.rawMin);
            tagObj.insert("raw_max", tag.rawMax);
            tagObj.insert("eng_min", tag.engMin);
            tagObj.insert("eng_max", tag.engMax);
            tagObj.insert("scaling_type", tag.scalingType);
            tagObj.insert("slope", tag.slope);
            tagObj.insert("offset", tag.offset);
            tagObj.insert("deadband", tag.deadband);
            tagObj.insert("enabled", tag.enabled);
            tagObj.insert("driver_id", tag.driverId);
            tagObj.insert("address_config", tag.addressConfig);
            tagObj.insert("sim_profile", tag.simProfile);
            tagObj.insert("software_filter", tag.softwareFilter);
            tagObj.insert("software_filter_config", tag.softwareFilterConfig);

            return HttpResponse::ok(tagObj);
        }
    }

    return HttpResponse::notFound("Tag not found");
}

HttpResponse RestApiHandler::handleCreateTag(const HttpRequest& request)
{
    Q_UNUSED(request)
    // TODO: Implement tag creation
    return HttpResponse::badRequest("Tag creation not implemented yet");
}

HttpResponse RestApiHandler::handleUpdateTag(const HttpRequest& request, int tagId)
{
    Q_UNUSED(request)
    Q_UNUSED(tagId)
    // TODO: Implement tag update
    return HttpResponse::badRequest("Tag update not implemented yet");
}

HttpResponse RestApiHandler::handleDeleteTag(const HttpRequest& request, int tagId)
{
    Q_UNUSED(request)
    Q_UNUSED(tagId)
    // TODO: Implement tag deletion
    return HttpResponse::badRequest("Tag deletion not implemented yet");
}

HttpResponse RestApiHandler::handleGetTagCurrent(const HttpRequest& request, int tagId)
{
    Q_UNUSED(request)

    QJsonObject current = m_db.getTagCurrentState(tagId);

    if (current.isEmpty()) {
        return HttpResponse::notFound("Tag current state not found");
    }

    return HttpResponse::ok(current);
}

HttpResponse RestApiHandler::handleGetAlarms(const HttpRequest& request)
{
    int limit = request.queryParam("limit", "100").toInt();
    int offset = request.queryParam("offset", "0").toInt();

    QJsonObject result;
    QJsonArray alarms;

    QVector<QJsonObject> alarmList = m_db.loadAlarms(limit, offset);

    for (const QJsonObject& alarm : alarmList) {
        alarms.append(alarm);
    }

    result.insert("alarms", alarms);
    result.insert("count", alarms.size());
    result.insert("limit", limit);
    result.insert("offset", offset);

    return HttpResponse::ok(result);
}

HttpResponse RestApiHandler::handleAckAlarm(const HttpRequest& request, qint64 alarmId)
{
    QString userName = request.jsonBody.value("user_name").toString("system");

    bool success = m_db.acknowledgeAlarm(alarmId, userName);

    if (success) {
        QJsonObject result;
        result.insert("alarm_id", alarmId);
        result.insert("acknowledged", true);
        result.insert("user_name", userName);
        return HttpResponse::ok(result);
    }

    return HttpResponse::serverError("Failed to acknowledge alarm");
}

HttpResponse RestApiHandler::handleGetDrivers(const HttpRequest& request)
{
    Q_UNUSED(request)

    QJsonObject result;
    QJsonArray drivers;

    QVector<DriverDefinition> driverList = m_db.loadDrivers();

    for (const DriverDefinition& driver : driverList) {
        QJsonObject driverObj;
        driverObj.insert("driver_id", driver.driverId);
        driverObj.insert("name", driver.name);
        driverObj.insert("type", driver.type);
        driverObj.insert("polling_interval_ms", driver.pollingIntervalMs);
        driverObj.insert("enabled", driver.enabled);

        // Parse connection_config
        QJsonDocument configDoc = QJsonDocument::fromJson(driver.connectionConfig.toUtf8());
        if (configDoc.isObject()) {
            driverObj.insert("connection_config", configDoc.object());
        } else {
            driverObj.insert("connection_config", driver.connectionConfig);
        }

        drivers.append(driverObj);
    }

    result.insert("drivers", drivers);
    result.insert("count", drivers.size());

    return HttpResponse::ok(result);
}

HttpResponse RestApiHandler::handleGetSystemStatus(const HttpRequest& request)
{
    Q_UNUSED(request)

    QJsonObject status;
    status.insert("status", "running");
    status.insert("version", "0.1.0");
    status.insert("timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    return HttpResponse::ok(status);
}
