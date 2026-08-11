#include "RestApiHandler.h"
#include "../storage/DbManager.h"
#include "../core/Models.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>


RestApiHandler::RestApiHandler(DbManager& db, DashboardManager* dashboardManager, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_dashboardManager(dashboardManager)
{
}

void RestApiHandler::setAuthenticator(const ApiAuthenticator::Config& config)
{
    m_auth.setConfig(config);
    qInfo() << "[API] Authenticator enabled:" << config.enabled;
}

HttpResponse RestApiHandler::handleRequest(const HttpRequest& request)
{
    // OPTIONS همیشه قبول است (برای CORS preflight)
    if (request.method == "OPTIONS") {
        return HttpResponse::ok();
    }

    // بررسی احراز هویت
    if (!m_auth.authenticate(request)) {
        HttpResponse res;
        res.statusCode = 401;
        res.errorMessage = "Unauthorized: invalid or missing API key";
        return res;
    }

    const QString path = request.path;

    // Health check
    if (path == "/api/v1/health" || path == "/health") {
        return handleHealthCheck(request);
    }

    // System status
    if (path == "/api/v1/system/status") {
        if (request.method == "GET") {
            return handleGetSystemStatus(request);
        }
        return HttpResponse::badRequest("Method not allowed");
    }

    // Tags collection
    if (path == "/api/v1/tags") {
        if (request.method == "GET") {
            return handleGetTags(request);
        } else if (request.method == "POST") {
            return handleCreateTag(request);
        }
        return HttpResponse::badRequest("Method not allowed");
    }

    // Tags individual + sub-resources
    if (path.startsWith("/api/v1/tags/")) {
        // /api/v1/tags/{id}/current
        if (path.endsWith("/current")) {
            qint64 tagId = 0;
            if (parseTagIdFromPath(path, tagId, "/current")) {
                if (request.method == "GET") {
                    return handleGetTagCurrent(request, tagId);
                }
                return HttpResponse::badRequest("Method not allowed");
            }
            return HttpResponse::badRequest("Invalid tag id");
        }

        // /api/v1/tags/{id}/history
        if (path.endsWith("/history")) {
            qint64 tagId = 0;
            if (parseTagIdFromPath(path, tagId, "/history")) {
                if (request.method == "GET") {
                    return handleGetTagHistory(request, tagId);
                }
                return HttpResponse::badRequest("Method not allowed");
            }
            return HttpResponse::badRequest("Invalid tag id");
        }

        // /api/v1/tags/{id}
        qint64 tagId = 0;
        if (parseTagIdFromPath(path, tagId)) {
            if (request.method == "GET") {
                return handleGetTag(request, tagId);
            } else if (request.method == "PUT") {
                return handleUpdateTag(request, tagId);
            } else if (request.method == "DELETE") {
                return handleDeleteTag(request, tagId);
            }
            return HttpResponse::badRequest("Method not allowed");
        }
        return HttpResponse::badRequest("Invalid tag id");
    }

    // Alarms
    if (path == "/api/v1/alarms") {
        if (request.method == "GET") {
            return handleGetAlarms(request);
        }
        return HttpResponse::badRequest("Method not allowed");
    }

    if (path.startsWith("/api/v1/alarms/") && path.endsWith("/ack")) {
        QString idStr = path.mid(15, path.length() - 15 - 4);
        bool ok = false;
        qint64 alarmId = idStr.toLongLong(&ok);
        if (ok && request.method == "POST") {
            return handleAckAlarm(request, alarmId);
        }
        return HttpResponse::badRequest("Invalid alarm id or method");
    }

    // Drivers
    if (path == "/api/v1/drivers") {
        if (request.method == "GET") {
            return handleGetDrivers(request);
        }
        return HttpResponse::badRequest("Method not allowed");
    }

    // Dashboards collection
    if (path == "/api/v1/dashboards") {
        if (request.method == "GET") {
            return handleGetDashboards(request);
        } else if (request.method == "POST") {
            return handleCreateDashboard(request);
        }
        return HttpResponse::badRequest("Method not allowed");
    }

    // Dashboards individual + sub-resources
    if (path.startsWith("/api/v1/dashboards/")) {

        // /api/v1/dashboards/{id}/content
        if (path.endsWith("/content")) {
            qint64 dashboardId = 0;
            if (parseIdFromPath(path, dashboardId, "/api/v1/dashboards/", "/content")) {
                if (request.method == "GET") {
                    return handleGetDashboardContent(request, dashboardId);
                } else if (request.method == "PUT") {
                    return handlePutDashboardContent(request, dashboardId);
                }
                return HttpResponse::badRequest("Method not allowed");
            }
            return HttpResponse::badRequest("Invalid dashboard id");
        }

        // /api/v1/dashboards/{id}/resources
        if (path.endsWith("/resources")) {
            qint64 dashboardId = 0;
            if (parseIdFromPath(path, dashboardId, "/api/v1/dashboards/", "/resources")) {
                if (request.method == "GET") {
                    return handleGetResources(request, dashboardId);
                }
                return HttpResponse::badRequest("Method not allowed");
            }
            return HttpResponse::badRequest("Invalid dashboard id");
        }

        // /api/v1/dashboards/{id}
        qint64 dashboardId = 0;
        if (parseIdFromPath(path, dashboardId, "/api/v1/dashboards/")) {
            if (request.method == "GET") {
                return handleGetDashboard(request, dashboardId);
            } else if (request.method == "PUT") {
                return handleUpdateDashboard(request, dashboardId);
            } else if (request.method == "DELETE") {
                return handleDeleteDashboard(request, dashboardId);
            }
            return HttpResponse::badRequest("Method not allowed");
        }
        return HttpResponse::badRequest("Invalid dashboard id");
    }


    return HttpResponse::notFound("Endpoint not found: " + path);
}

// ============================================================
// Tags Handlers
// ============================================================

HttpResponse RestApiHandler::handleGetTags(const HttpRequest& request)
{
    Q_UNUSED(request)

    QJsonObject result;
    QJsonArray tags;

    const QVector<TagDefinition> tagList = m_db.loadTags();

    for (const TagDefinition& tag : tagList) {
        tags.append(tagToJson(tag));
    }

    result.insert("tags", tags);
    result.insert("count", tags.size());

    return HttpResponse::ok(result);
}

HttpResponse RestApiHandler::handleGetTag(const HttpRequest& request, qint64 tagId)
{
    Q_UNUSED(request)

    const QVector<TagDefinition> tagList = m_db.loadTags();

    for (const TagDefinition& tag : tagList) {
        if (tag.tagId == tagId) {
            return HttpResponse::ok(tagToJson(tag));
        }
    }

    return HttpResponse::notFound("Tag not found");
}

HttpResponse RestApiHandler::handleCreateTag(const HttpRequest& request)
{
    if (request.jsonBody.isEmpty()) {
        return HttpResponse::badRequest("Request body is required");
    }

    TagDefinition tag;
    if (!jsonToTag(request.jsonBody, tag, false)) {
        return HttpResponse::badRequest("Invalid tag definition");
    }

    if (tag.tagId <= 0) {
        return HttpResponse::badRequest("tag_id is required and must be positive");
    }

    if (tag.tagName.isEmpty()) {
        return HttpResponse::badRequest("tag_name is required");
    }

    // بررسی وجود نداشتن tag با همین id
    const QVector<TagDefinition> existingTags = m_db.loadTags();
    for (const TagDefinition& existing : existingTags) {
        if (existing.tagId == tag.tagId) {
            return HttpResponse::badRequest("Tag with this id already exists");
        }
    }

    if (m_db.upsertTag(tag)) {
        QJsonObject result;
        result.insert("created", true);
        result.insert("tag_id", tag.tagId);
        return HttpResponse::created(result);
    }

    return HttpResponse::serverError("Failed to create tag");
}

HttpResponse RestApiHandler::handleUpdateTag(const HttpRequest& request, qint64 tagId)
{
    if (request.jsonBody.isEmpty()) {
        return HttpResponse::badRequest("Request body is required");
    }

    // پیدا کردن tag موجود
    TagDefinition tag;
    bool found = false;
    const QVector<TagDefinition> tagList = m_db.loadTags();
    for (const TagDefinition& t : tagList) {
        if (t.tagId == tagId) {
            tag = t;
            found = true;
            break;
        }
    }

    if (!found) {
        return HttpResponse::notFound("Tag not found");
    }

    // اعمال تغییرات از request
    if (!jsonToTag(request.jsonBody, tag, true)) {
        return HttpResponse::badRequest("Invalid tag definition");
    }

    // مطمئن شو tag_id تغییر نکند
    tag.tagId = tagId;

    if (m_db.upsertTag(tag)) {
        QJsonObject result;
        result.insert("updated", true);
        result.insert("tag_id", tagId);
        return HttpResponse::ok(result);
    }

    return HttpResponse::serverError("Failed to update tag");
}

HttpResponse RestApiHandler::handleDeleteTag(const HttpRequest& request, qint64 tagId)
{
    Q_UNUSED(request)

    if (m_db.deleteTag(tagId)) {
        QJsonObject result;
        result.insert("deleted", true);
        result.insert("tag_id", tagId);
        return HttpResponse::ok(result);
    }

    return HttpResponse::notFound("Tag not found or delete failed");
}

HttpResponse RestApiHandler::handleGetTagCurrent(const HttpRequest& request, qint64 tagId)
{
    Q_UNUSED(request)

    QJsonObject current = m_db.getTagCurrentState(int(tagId));

    if (current.isEmpty()) {
        return HttpResponse::notFound("Tag current state not found");
    }

    return HttpResponse::ok(current);
}

HttpResponse RestApiHandler::handleGetTagHistory(const HttpRequest& request, qint64 tagId)
{
    // Parse query parameters
    const QString fromStr = request.queryParam("from");
    const QString toStr = request.queryParam("to");
    const QString interval = request.queryParam("interval");
    const int limit = request.queryParam("limit", "1000").toInt();

    QDateTime fromTime;
    QDateTime toTime;

    if (fromStr.isEmpty()) {
        fromTime = QDateTime::currentDateTimeUtc().addSecs(-3600); // پیش‌فرض: ۱ ساعت اخیر
    } else {
        fromTime = QDateTime::fromString(fromStr, Qt::ISODate);
        if (!fromTime.isValid()) {
            fromTime = QDateTime::fromString(fromStr, Qt::ISODateWithMs);
        }
    }

    if (toStr.isEmpty()) {
        toTime = QDateTime::currentDateTimeUtc();
    } else {
        toTime = QDateTime::fromString(toStr, Qt::ISODate);
        if (!toTime.isValid()) {
            toTime = QDateTime::fromString(toStr, Qt::ISODateWithMs);
        }
    }

    if (!fromTime.isValid() || !toTime.isValid()) {
        return HttpResponse::badRequest("Invalid from or to date format. Use ISO 8601.");
    }

    if (fromTime >= toTime) {
        return HttpResponse::badRequest("from must be before to");
    }

    const QVector<QJsonObject> history = m_db.queryTagHistory(
        tagId, fromTime, toTime, interval, limit
    );

    QJsonObject result;
    QJsonArray points;
    for (const QJsonObject& point : history) {
        points.append(point);
    }

    result.insert("tag_id", tagId);
    result.insert("from", fromTime.toString(Qt::ISODateWithMs));
    result.insert("to", toTime.toString(Qt::ISODateWithMs));
    result.insert("interval", interval);
    result.insert("count", points.size());
    result.insert("points", points);

    return HttpResponse::ok(result);
}

// ============================================================
// Alarms Handlers
// ============================================================

HttpResponse RestApiHandler::handleGetAlarms(const HttpRequest& request)
{
    const int limit = request.queryParam("limit", "100").toInt();
    const int offset = request.queryParam("offset", "0").toInt();

    QJsonObject result;
    QJsonArray alarms;

    const QVector<QJsonObject> alarmList = m_db.loadAlarms(limit, offset);

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
    QString userName = "system";
    if (request.jsonBody.contains("user_name")) {
        userName = request.jsonBody.value("user_name").toString();
    }

    if (m_db.acknowledgeAlarm(alarmId, userName)) {
        QJsonObject result;
        result.insert("alarm_id", alarmId);
        result.insert("acknowledged", true);
        result.insert("user_name", userName);
        return HttpResponse::ok(result);
    }

    return HttpResponse::serverError("Failed to acknowledge alarm");
}

// ============================================================
// Drivers Handler
// ============================================================

HttpResponse RestApiHandler::handleGetDrivers(const HttpRequest& request)
{
    Q_UNUSED(request)

    QJsonObject result;
    QJsonArray drivers;

    const QVector<DriverDefinition> driverList = m_db.loadDrivers();

    for (const DriverDefinition& driver : driverList) {
        QJsonObject driverObj;
        driverObj.insert("driver_id", driver.driverId);
        driverObj.insert("name", driver.name);
        driverObj.insert("type", driver.type);
        driverObj.insert("polling_interval_ms", driver.pollingIntervalMs);
        driverObj.insert("enabled", driver.enabled);

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

// ============================================================
// System Handlers
// ============================================================

HttpResponse RestApiHandler::handleGetSystemStatus(const HttpRequest& request)
{
    Q_UNUSED(request)

    QJsonObject status;
    status.insert("status", "running");
    status.insert("version", "0.1.0");
    status.insert("timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    return HttpResponse::ok(status);
}

HttpResponse RestApiHandler::handleHealthCheck(const HttpRequest& request)
{
    Q_UNUSED(request)

    QJsonObject health;
    health.insert("status", "ok");
    health.insert("timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    return HttpResponse::ok(health);
}

// ============================================================
// Helpers
// ============================================================

bool RestApiHandler::parseTagIdFromPath(const QString& path, qint64& tagId, const QString& suffix) const
{
    QString workingPath = path;

    if (!suffix.isEmpty() && workingPath.endsWith(suffix)) {
        workingPath = workingPath.left(workingPath.length() - suffix.length());
    }

    const QString prefix = "/api/v1/tags/";
    if (!workingPath.startsWith(prefix)) {
        return false;
    }

    const QString idStr = workingPath.mid(prefix.length());
    bool ok = false;
    tagId = idStr.toLongLong(&ok);

    return ok && tagId > 0;
}

QJsonObject RestApiHandler::tagToJson(const TagDefinition& tag) const
{
    QJsonObject obj;
    obj.insert("tag_id", tag.tagId);
    obj.insert("tag_name", tag.tagName);
    obj.insert("source_type", tag.sourceType);
    obj.insert("data_type", tag.dataType);
    obj.insert("eng_units", tag.engUnits);
    obj.insert("raw_min", tag.rawMin);
    obj.insert("raw_max", tag.rawMax);
    obj.insert("eng_min", tag.engMin);
    obj.insert("eng_max", tag.engMax);
    obj.insert("scaling_type", tag.scalingType);
    obj.insert("slope", tag.slope);
    obj.insert("offset", tag.offset);
    obj.insert("deadband", tag.deadband);
    obj.insert("storage_deadband", tag.storageDeadband);
    obj.insert("alarm_hysteresis", tag.alarmHysteresis);
    obj.insert("heartbeat_interval_ms", tag.heartbeatIntervalMs);
    obj.insert("software_filter", tag.softwareFilter);
    obj.insert("software_filter_config", tag.softwareFilterConfig);
    obj.insert("sim_profile", tag.simProfile);
    obj.insert("driver_id", tag.driverId);
    obj.insert("address_config", tag.addressConfig);
    obj.insert("enabled", tag.enabled);
    obj.insert("clamp_enabled", tag.clampEnabled);
    return obj;
}

bool RestApiHandler::jsonToTag(const QJsonObject& json, TagDefinition& tag, bool isUpdate) const
{
    // فیلدهای اجباری برای create
    if (!isUpdate) {
        if (!json.contains("tag_id") || !json.contains("tag_name")) {
            return false;
        }
    }

    if (json.contains("tag_id")) {
        tag.tagId = json.value("tag_id").toVariant().toLongLong();;
    }
    if (json.contains("tag_name")) {
        tag.tagName = json.value("tag_name").toString();
    }
    if (json.contains("source_type")) {
        tag.sourceType = json.value("source_type").toString();
    }
    if (json.contains("data_type")) {
        tag.dataType = json.value("data_type").toString();
    }
    if (json.contains("eng_units")) {
        tag.engUnits = json.value("eng_units").toString();
    }
    if (json.contains("raw_min")) {
        tag.rawMin = json.value("raw_min").toDouble();
    }
    if (json.contains("raw_max")) {
        tag.rawMax = json.value("raw_max").toDouble();
    }
    if (json.contains("eng_min")) {
        tag.engMin = json.value("eng_min").toDouble();
    }
    if (json.contains("eng_max")) {
        tag.engMax = json.value("eng_max").toDouble();
    }
    if (json.contains("scaling_type")) {
        tag.scalingType = json.value("scaling_type").toString();
    }
    if (json.contains("slope")) {
        tag.slope = json.value("slope").toDouble();
    }
    if (json.contains("offset")) {
        tag.offset = json.value("offset").toDouble();
    }
    if (json.contains("deadband")) {
        tag.deadband = json.value("deadband").toDouble();
    }
    if (json.contains("storage_deadband")) {
        tag.storageDeadband = json.value("storage_deadband").toDouble();
    }
    if (json.contains("alarm_hysteresis")) {
        tag.alarmHysteresis = json.value("alarm_hysteresis").toDouble();
    }
    if (json.contains("heartbeat_interval_ms")) {
        tag.heartbeatIntervalMs = json.value("heartbeat_interval_ms").toInt();
    }
    if (json.contains("software_filter")) {
        tag.softwareFilter = json.value("software_filter").toString();
    }
    if (json.contains("software_filter_config")) {
        tag.softwareFilterConfig = json.value("software_filter_config").toString();
    }
    if (json.contains("sim_profile")) {
        tag.simProfile = json.value("sim_profile").toString();
    }
    if (json.contains("driver_id")) {
        tag.driverId = json.value("driver_id").toVariant().toLongLong();
    }
    if (json.contains("address_config")) {
        if (json.value("address_config").isObject()) {
            QJsonDocument doc(json.value("address_config").toObject());
            tag.addressConfig = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
        } else {
            tag.addressConfig = json.value("address_config").toString();
        }
    }
    if (json.contains("enabled")) {
        tag.enabled = json.value("enabled").toBool();
    }
    if (json.contains("clamp_enabled")) {
        tag.clampEnabled = json.value("clamp_enabled").toBool();
    }

    return true;
}

HttpResponse RestApiHandler::handleGetDashboards(const HttpRequest& request)
{
    Q_UNUSED(request)

    if (!m_dashboardManager) {
        return HttpResponse::serverError("DashboardManager not initialized");
    }

    QJsonObject result;
    QJsonArray dashboards;

    const QVector<DashboardDefinition> dashboardList = m_dashboardManager->listDashboards();

    for (const DashboardDefinition& d : dashboardList) {
        dashboards.append(dashboardToJson(d));
    }

    result.insert("dashboards", dashboards);
    result.insert("count", dashboards.size());

    return HttpResponse::ok(result);
}

HttpResponse RestApiHandler::handleGetDashboard(const HttpRequest& request, qint64 dashboardId)
{
    Q_UNUSED(request)

    if (!m_dashboardManager) {
        return HttpResponse::serverError("DashboardManager not initialized");
    }

    const DashboardDefinition d = m_dashboardManager->getDashboard(dashboardId);

    if (d.dashboardId == 0) {
        return HttpResponse::notFound("Dashboard not found");
    }

    return HttpResponse::ok(dashboardToJson(d));
}

HttpResponse RestApiHandler::handleCreateDashboard(const HttpRequest& request)
{
    if (!m_dashboardManager) {
        return HttpResponse::serverError("DashboardManager not initialized");
    }

    if (request.jsonBody.isEmpty()) {
        return HttpResponse::badRequest("Request body is required");
    }

    DashboardDefinition dashboard;
    if (!jsonToDashboard(request.jsonBody, dashboard, false)) {
        return HttpResponse::badRequest("Invalid dashboard definition");
    }

    if (dashboard.name.isEmpty()) {
        return HttpResponse::badRequest("name is required");
    }

    // اعتبارسنجی dashboard_type
    QStringList validTypes = {"simple", "qml", "html"};
    if (!validTypes.contains(dashboard.dashboardType)) {
        return HttpResponse::badRequest("Invalid dashboard_type. Must be: simple, qml, html");
    }

    const qint64 newId = m_dashboardManager->createDashboard(dashboard);

    if (newId > 0) {
        QJsonObject result;
        result.insert("created", true);
        result.insert("dashboard_id", newId);
        result.insert("dashboard_type", dashboard.dashboardType);
        return HttpResponse::created(result);
    }

    return HttpResponse::serverError("Failed to create dashboard");
}

HttpResponse RestApiHandler::handleUpdateDashboard(const HttpRequest& request, qint64 dashboardId)
{
    if (!m_dashboardManager) {
        return HttpResponse::serverError("DashboardManager not initialized");
    }

    if (request.jsonBody.isEmpty()) {
        return HttpResponse::badRequest("Request body is required");
    }

    DashboardDefinition existing = m_dashboardManager->getDashboard(dashboardId);
    if (existing.dashboardId == 0) {
        return HttpResponse::notFound("Dashboard not found");
    }

    if (!jsonToDashboard(request.jsonBody, existing, true)) {
        return HttpResponse::badRequest("Invalid dashboard definition");
    }

    existing.dashboardId = dashboardId;

    if (m_dashboardManager->updateDashboard(existing)) {
        QJsonObject result;
        result.insert("updated", true);
        result.insert("dashboard_id", dashboardId);
        return HttpResponse::ok(result);
    }

    return HttpResponse::serverError("Failed to update dashboard");
}

HttpResponse RestApiHandler::handleDeleteDashboard(const HttpRequest& request, qint64 dashboardId)
{
    Q_UNUSED(request)

    if (!m_dashboardManager) {
        return HttpResponse::serverError("DashboardManager not initialized");
    }

    if (m_dashboardManager->deleteDashboard(dashboardId)) {
        QJsonObject result;
        result.insert("deleted", true);
        result.insert("dashboard_id", dashboardId);
        return HttpResponse::ok(result);
    }

    return HttpResponse::notFound("Dashboard not found or delete failed");
}

HttpResponse RestApiHandler::handleGetDashboardContent(const HttpRequest& request, qint64 dashboardId)
{
    Q_UNUSED(request)

    if (!m_dashboardManager) {
        return HttpResponse::serverError("DashboardManager not initialized");
    }

    DashboardDefinition dashboard = m_dashboardManager->getDashboard(dashboardId);
    if (dashboard.dashboardId == 0) {
        return HttpResponse::notFound("Dashboard not found");
    }

    const QString content = m_dashboardManager->getDashboardContent(dashboardId);
    if (content.isEmpty()) {
        return HttpResponse::notFound("Dashboard content not found");
    }

    QJsonObject result;
    result.insert("dashboard_id", dashboardId);
    result.insert("dashboard_type", dashboard.dashboardType);
    result.insert("content", content);

    return HttpResponse::ok(result);
}

HttpResponse RestApiHandler::handlePutDashboardContent(const HttpRequest& request, qint64 dashboardId)
{
    if (!m_dashboardManager) {
        return HttpResponse::serverError("DashboardManager not initialized");
    }

    if (!request.jsonBody.contains("content")) {
        return HttpResponse::badRequest("Missing 'content' field");
    }

    const QString content = request.jsonBody.value("content").toString();

    if (m_dashboardManager->saveDashboardContent(dashboardId, content)) {
        QJsonObject result;
        result.insert("saved", true);
        result.insert("dashboard_id", dashboardId);
        return HttpResponse::ok(result);
    }

    return HttpResponse::serverError("Failed to save dashboard content");
}

HttpResponse RestApiHandler::handleGetResources(const HttpRequest& request, qint64 dashboardId)
{
    Q_UNUSED(request)

    if (!m_dashboardManager) {
        return HttpResponse::serverError("DashboardManager not initialized");
    }

    const QStringList resources = m_dashboardManager->listResources(dashboardId);

    QJsonObject result;
    QJsonArray resourcesArray;
    for (const QString& r : resources) {
        resourcesArray.append(r);
    }

    result.insert("dashboard_id", dashboardId);
    result.insert("resources", resourcesArray);
    result.insert("count", resourcesArray.size());

    return HttpResponse::ok(result);
}

QJsonObject RestApiHandler::dashboardToJson(const DashboardDefinition& dashboard) const
{
    QJsonObject obj;
    obj.insert("dashboard_id", dashboard.dashboardId);
    obj.insert("name", dashboard.name);
    obj.insert("description", dashboard.description);
    obj.insert("owner", dashboard.owner);
    obj.insert("dashboard_type", dashboard.dashboardType);
    obj.insert("is_public", dashboard.isPublic);

    if (dashboard.createdAt.isValid()) {
        obj.insert("created_at", dashboard.createdAt.toString(Qt::ISODateWithMs));
    }
    if (dashboard.updatedAt.isValid()) {
        obj.insert("updated_at", dashboard.updatedAt.toString(Qt::ISODateWithMs));
    }

    return obj;
}

bool RestApiHandler::jsonToDashboard(const QJsonObject& json, DashboardDefinition& dashboard, bool isUpdate) const
{
    if (!isUpdate) {
        dashboard.name = "";
        dashboard.description = "";
        dashboard.owner = "system";
        dashboard.dashboardType = "simple";
        dashboard.isPublic = true;
    }

    if (!isUpdate) {
        if (!json.contains("name")) {
            return false;
        }
    }

    if (json.contains("name")) {
        dashboard.name = json.value("name").toString();
    }
    if (json.contains("description")) {
        dashboard.description = json.value("description").toString();
    }
    if (json.contains("owner")) {
        dashboard.owner = json.value("owner").toString();
    }
    if (json.contains("dashboard_type")) {
        dashboard.dashboardType = json.value("dashboard_type").toString();
    }
    if (json.contains("is_public")) {
        dashboard.isPublic = json.value("is_public").toBool();
    }

    return true;
}

bool RestApiHandler::parseIdFromPath(const QString& path, qint64& id, const QString& prefix, const QString& suffix) const
{
    QString workingPath = path;

    if (!suffix.isEmpty() && workingPath.endsWith(suffix)) {
        workingPath = workingPath.left(workingPath.length() - suffix.length());
    }

    if (!workingPath.startsWith(prefix)) {
        return false;
    }

    const QString idStr = workingPath.mid(prefix.length());
    bool ok = false;
    id = idStr.toLongLong(&ok);

    return ok && id > 0;
}

