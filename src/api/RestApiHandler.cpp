#include "RestApiHandler.h"
#include "../storage/DbManager.h"
#include "../core/Models.h"
#include "ReportGenerator.h"
#include "WebSocketHandler.h"
#include "../api/UserManager.h"
#include "../drivers/DriverManager.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>


RestApiHandler::RestApiHandler(DbManager& db, DashboardManager* dashboardManager,ReportGenerator* reportGenerator, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_dashboardManager(dashboardManager)
    , m_reportGenerator(reportGenerator)
{
}

void RestApiHandler::setAuthenticator(const ApiAuthenticator::Config& config)
{
    m_auth.setConfig(config);
    qInfo() << "[API] Authenticator enabled:" << config.enabled;
}

void RestApiHandler::setWebSocketHandler(WebSocketHandler* ws)
{
    m_wsHandler = ws;
}

void RestApiHandler::setUserManager(UserManager* um) { m_userManager = um; }

void RestApiHandler::setDriverManager(DriverManager* dm) { m_driverManager = dm; }

void RestApiHandler::refreshDriverManagerConfig()
{
    if (!m_driverManager) return;
    AppConfig cfg;
    cfg.drivers = m_db.loadDrivers();
    cfg.tags = m_db.loadTags();
    m_driverManager->updateConfig(cfg);
}

bool RestApiHandler::jsonToDriver(const QJsonObject& json, DriverDefinition& def, bool isUpdate, QString& error) const
{
    if (!isUpdate) {
        if (!json.contains("name") || json.value("name").toString().trimmed().isEmpty()) {
            error = "name is required"; return false;
        }
        if (!json.contains("type")) { error = "type is required"; return false; }
    }

    if (json.contains("name"))
        def.name = json.value("name").toString().trimmed();
    if (json.contains("type"))
        def.type = json.value("type").toString().trimmed().toLower();

    QStringList validTypes;
    validTypes << "simulator" << "modbus_tcp" << "modbus_rtu" << "mqtt" << "opc_ua";
    if (!validTypes.contains(def.type)) {
        error = "Invalid type. Must be: simulator, modbus_tcp, modbus_rtu, mqtt, opc_ua";
        return false;
    }

    if (json.contains("connection_config")) {
        if (json.value("connection_config").isObject()) {
            QJsonDocument doc(json.value("connection_config").toObject());
            def.connectionConfig = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
        } else {
            const QString s = json.value("connection_config").toString();
            if (!QJsonDocument::fromJson(s.toUtf8()).isObject()) {
                error = "connection_config must be a valid JSON object"; return false;
            }
            def.connectionConfig = s;
        }
    }

    if (json.contains("polling_interval_ms")) {
        int p = json.value("polling_interval_ms").toInt(1000);
        def.pollingIntervalMs = (p < 100) ? 100 : p;
    }

    if (json.contains("enabled"))
        def.enabled = json.value("enabled").toBool(true);

    return true;
}

HttpResponse RestApiHandler::handleCreateDriver(const HttpRequest& request)
{
    if (request.jsonBody.isEmpty())
        return HttpResponse::badRequest("Request body is required");

    DriverDefinition def;
    QString error;
    if (!jsonToDriver(request.jsonBody, def, false, error))
        return HttpResponse::badRequest(error);

    const qint64 newId = m_db.insertDriver(def);
    if (newId <= 0)
        return HttpResponse::serverError("Failed to create driver");

    refreshDriverManagerConfig();

    bool started = false;
    if (def.enabled && m_driverManager)
        started = m_driverManager->startDriver(newId);   // ✅ بالا آمدن همان لحظه

    QJsonObject result;
    result.insert("created", true);
    result.insert("driver_id", newId);
    result.insert("started", started);
    return HttpResponse::created(result);
}

HttpResponse RestApiHandler::handleUpdateDriver(const HttpRequest& request, qint64 driverId)
{
    DriverDefinition existing = m_db.loadDriver(driverId);
    if (existing.driverId == 0)
        return HttpResponse::notFound("Driver not found");

    DriverDefinition def = existing;
    QString error;
    if (!jsonToDriver(request.jsonBody, def, true, error))
        return HttpResponse::badRequest(error);
    def.driverId = driverId;

    if (!m_db.updateDriver(def))
        return HttpResponse::serverError("Failed to update driver");

    const bool wasRunning = m_driverManager && m_driverManager->isDriverRunning(driverId);

    refreshDriverManagerConfig();

    bool restarted = false;
    if (m_driverManager) {
        if (wasRunning)
            m_driverManager->stopDriver(driverId);      // ✅ restart با کانفیگ جدید
        if (def.enabled)
            restarted = m_driverManager->startDriver(driverId);
    }

    QJsonObject result;
    result.insert("updated", true);
    result.insert("driver_id", driverId);
    result.insert("restarted", restarted);
    return HttpResponse::ok(result);
}

HttpResponse RestApiHandler::handleDeleteDriver(const HttpRequest& request, qint64 driverId)
{
    Q_UNUSED(request)
    if (m_db.loadDriver(driverId).driverId == 0)
        return HttpResponse::notFound("Driver not found");

    const int tagsCount = m_db.tagCountForDriver(driverId);
    if (tagsCount > 0) {
        HttpResponse r;
        r.statusCode = 409;
        r.errorMessage = QString("Cannot delete driver: %1 tag(s) reference it. Delete or move tags first.").arg(tagsCount);
        return r;
    }

    if (m_driverManager)
        m_driverManager->stopDriver(driverId);

    if (!m_db.deleteDriver(driverId))
        return HttpResponse::serverError("Failed to delete driver");

    refreshDriverManagerConfig();

    QJsonObject result;
    result.insert("deleted", true);
    result.insert("driver_id", driverId);
    return HttpResponse::ok(result);
}
QString RestApiHandler::extractBearerToken(const HttpRequest& request) const
{
    if (request.headers.contains("authorization")) {
        QString auth = request.headers.value("authorization");
        if (auth.startsWith("Bearer ", Qt::CaseInsensitive))
            return auth.mid(7).trimmed();
    }
    return request.queryParam("token");
}

bool RestApiHandler::authenticateRequest(const HttpRequest& request, UserDefinition& user)
{
    // 1) API Key → نقش admin
    const QString apiKey = m_auth.extractApiKey(request);
    if (!apiKey.isEmpty() && m_auth.isValidApiKey(apiKey)) {
        user.role = "admin";
        user.username = "api-key";
        return true;
    }
    // 2) Bearer token
    const QString token = extractBearerToken(request);
    if (!token.isEmpty() && m_userManager && m_userManager->validateToken(token, user))
        return true;
    return false;
}

HttpResponse RestApiHandler::handleLogin(const HttpRequest& request)
{
    if (!m_userManager) return HttpResponse::serverError("UserManager not initialized");
    const QString username = request.jsonBody.value("username").toString();
    const QString password = request.jsonBody.value("password").toString();
    if (username.isEmpty() || password.isEmpty())
        return HttpResponse::badRequest("username and password are required");

    QJsonObject res = m_userManager->login(username, password);
    if (!res.value("ok").toBool()) {
        HttpResponse r; r.statusCode = 401;
        r.errorMessage = res.value("error").toString();
        return r;
    }
    return HttpResponse::ok(res.value("data").toObject());
}

HttpResponse RestApiHandler::handleLogout(const HttpRequest& request)
{
    if (!m_userManager) return HttpResponse::serverError("UserManager not initialized");
    const QString token = extractBearerToken(request);
    QJsonObject result;
    result.insert("logged_out", m_userManager->logout(token));
    return HttpResponse::ok(result);
}

HttpResponse RestApiHandler::handleMe(const HttpRequest& request)
{
    if (!m_userManager) return HttpResponse::serverError("UserManager not initialized");
    const QString token = extractBearerToken(request);
    QJsonObject res = m_userManager->me(token);
    if (!res.value("ok").toBool()) {
        HttpResponse r; r.statusCode = 401;
        r.errorMessage = res.value("error").toString();
        return r;
    }
    return HttpResponse::ok(res.value("data").toObject());
}

HttpResponse RestApiHandler::handleGetUsers(const HttpRequest& request)
{
    Q_UNUSED(request)
    QJsonObject result;
    QJsonArray arr;
    const QVector<UserDefinition> users = m_db.loadUsers();
    for (const UserDefinition& u : users) {
        QJsonObject o;
        o.insert("user_id", u.userId);
        o.insert("username", u.username);
        o.insert("display_name", u.displayName);
        o.insert("role", u.role);
        o.insert("is_active", u.isActive);
        arr.append(o);
    }
    result.insert("users", arr);
    result.insert("count", arr.size());
    return HttpResponse::ok(result);
}

HttpResponse RestApiHandler::handleCreateUser(const HttpRequest& request)
{
    if (!m_userManager) return HttpResponse::serverError("UserManager not initialized");
    const QString username = request.jsonBody.value("username").toString();
    const QString password = request.jsonBody.value("password").toString();
    if (username.isEmpty() || password.isEmpty())
        return HttpResponse::badRequest("username and password are required");

    QString role = request.jsonBody.value("role").toString("operator");
    QStringList validRoles; validRoles << "admin" << "operator" << "viewer";
    if (!validRoles.contains(role))
        return HttpResponse::badRequest("Invalid role");

    if (m_db.loadUserByUsername(username).userId != 0)
        return HttpResponse::badRequest("Username already exists");

    const QString salt = UserManager::generateSalt();
    const QString hash = UserManager::hashPassword(password, salt);
    const qint64 newId = m_db.insertUserRaw(username, hash, salt,
        request.jsonBody.value("display_name").toString(), role);

    if (newId > 0) {
        QJsonObject result;
        result.insert("created", true);
        result.insert("user_id", newId);
        return HttpResponse::created(result);
    }
    return HttpResponse::serverError("Failed to create user");
}

HttpResponse RestApiHandler::handleUpdateUser(const HttpRequest& request, qint64 userId)
{
    UserDefinition user = m_db.loadUserById(userId);
    if (user.userId == 0) return HttpResponse::notFound("User not found");

    if (request.jsonBody.contains("display_name"))
        user.displayName = request.jsonBody.value("display_name").toString();
    if (request.jsonBody.contains("role")) {
        QString role = request.jsonBody.value("role").toString();
        QStringList validRoles; validRoles << "admin" << "operator" << "viewer";
        if (!validRoles.contains(role)) return HttpResponse::badRequest("Invalid role");
        user.role = role;
    }
    if (request.jsonBody.contains("is_active"))
        user.isActive = request.jsonBody.value("is_active").toBool();

    if (m_db.updateUser(user)) {
        QJsonObject result;
        result.insert("updated", true);
        result.insert("user_id", userId);
        return HttpResponse::ok(result);
    }
    return HttpResponse::serverError("Failed to update user");
}

HttpResponse RestApiHandler::handleDeleteUser(const HttpRequest& request, qint64 userId)
{
    Q_UNUSED(request)
    if (m_db.deleteUser(userId)) {
        QJsonObject result;
        result.insert("deleted", true);
        result.insert("user_id", userId);
        return HttpResponse::ok(result);
    }
    return HttpResponse::notFound("User not found or delete failed");
}

HttpResponse RestApiHandler::handleUserChangePassword(const HttpRequest& request, qint64 userId)
{
    if (!m_userManager) return HttpResponse::serverError("UserManager not initialized");
    const QString newPassword = request.jsonBody.value("new_password").toString();
    if (newPassword.isEmpty()) return HttpResponse::badRequest("new_password is required");

    UserDefinition user = m_db.loadUserById(userId);
    if (user.userId == 0) return HttpResponse::notFound("User not found");

    const QString salt = UserManager::generateSalt();
    const QString hash = UserManager::hashPassword(newPassword, salt);
    if (m_db.updateUserPassword(userId, hash, salt)) {
        QJsonObject result;
        result.insert("password_changed", true);
        return HttpResponse::ok(result);
    }
    return HttpResponse::serverError("Failed to change password");
}


HttpResponse RestApiHandler::handleRequest(const HttpRequest& request)
{
    // OPTIONS همیشه قبول است (برای CORS preflight)
    if (request.method == "OPTIONS") {
        return HttpResponse::ok();
    }
    // ✅ احراز هویت + نقش
    if (!m_auth.isPublicPath(request.path) && m_auth.isEnabled()) {
        UserDefinition user;
        if (!authenticateRequest(request, user)) {
            HttpResponse r; r.statusCode = 401;
            r.errorMessage = "Unauthorized: invalid or missing credentials";
            return r;
        }
        if (m_userManager && !m_userManager->can(user, request.method, request.path)) {
            HttpResponse r; r.statusCode = 403;
            r.errorMessage = "Forbidden: insufficient role";
            return r;
        }
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
        if (request.method == "GET")  return handleGetDrivers(request);   // handler موجود
        if (request.method == "POST") return handleCreateDriver(request);
        return HttpResponse::badRequest("Method not allowed");
    }

    if (path.startsWith("/api/v1/drivers/")) {
        qint64 driverId = 0;
        if (parseIdFromPath(path, driverId, "/api/v1/drivers/")) {
            if (request.method == "PUT")    return handleUpdateDriver(request, driverId);
            if (request.method == "DELETE") return handleDeleteDriver(request, driverId);
            return HttpResponse::badRequest("Method not allowed");
        }
        return HttpResponse::badRequest("Invalid driver id");
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

    // Reports
    if (path == "/api/v1/reports/tag-history") {
        if (request.method == "GET") {
            return handleTagHistoryReport(request);
        }
        return HttpResponse::badRequest("Method not allowed");
    }

    if (path == "/api/v1/reports/alarms") {
        if (request.method == "GET") {
            return handleAlarmReport(request);
        }
        return HttpResponse::badRequest("Method not allowed");
    }

    if (path == "/api/v1/reports/daily-summary") {
        if (request.method == "GET") {
            return handleDailySummaryReport(request);
        }
        return HttpResponse::badRequest("Method not allowed");
    }

    // Auth
    if (request.path == "/api/v1/auth/login" && request.method == "POST") return handleLogin(request);
    if (request.path == "/api/v1/auth/logout" && request.method == "POST") return handleLogout(request);
    if (request.path == "/api/v1/auth/me" && request.method == "GET") return handleMe(request);

    // Users
    if (request.path == "/api/v1/users") {
        if (request.method == "GET") return handleGetUsers(request);
        if (request.method == "POST") return handleCreateUser(request);
        return HttpResponse::badRequest("Method not allowed");
    }
    if (request.path.startsWith("/api/v1/users/")) {
        if (request.path.endsWith("/password")) {
            qint64 id = 0;
            if (parseIdFromPath(request.path, id, "/api/v1/users/", "/password")) {
                if (request.method == "PUT") return handleUserChangePassword(request, id);
                return HttpResponse::badRequest("Method not allowed");
            }
            return HttpResponse::badRequest("Invalid user id");
        }
        qint64 id = 0;
        if (parseIdFromPath(request.path, id, "/api/v1/users/")) {
            if (request.method == "GET") { /* می‌تواند جزئیات برگرداند */ }
            if (request.method == "PUT") return handleUpdateUser(request, id);
            if (request.method == "DELETE") return handleDeleteUser(request, id);
            return HttpResponse::badRequest("Method not allowed");
        }
        return HttpResponse::badRequest("Invalid user id");
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
        if (m_wsHandler) {
            m_wsHandler->publishAlarmAck(alarmId, 0, userName);
        }
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

HttpResponse RestApiHandler::handleTagHistoryReport(const HttpRequest& request)
{
    if (!m_reportGenerator) {
        return HttpResponse::serverError("ReportGenerator not initialized");
    }

    TagHistoryQuery query;

    // Parse tag_ids
    const QString tagIdsStr = request.queryParam("tag_ids");
    if (tagIdsStr.isEmpty()) {
        return HttpResponse::badRequest("Missing 'tag_ids' parameter");
    }
    query.tagIds = parseTagIdsParam(tagIdsStr);

    if (query.tagIds.isEmpty()) {
        return HttpResponse::badRequest("Invalid 'tag_ids' parameter");
    }

    // Parse from
    QDateTime defaultFrom = QDateTime::currentDateTimeUtc().addSecs(-3600);
    query.from = parseDateTimeParam(request.queryParam("from"), defaultFrom);

    // Parse to
    QDateTime defaultTo = QDateTime::currentDateTimeUtc();
    query.to = parseDateTimeParam(request.queryParam("to"), defaultTo);

    // Parse interval
    query.interval = request.queryParam("interval", "raw");

    // Parse limit
    query.limit = request.queryParam("limit", "10000").toInt();
    if (query.limit <= 0 || query.limit > 100000) {
        query.limit = 10000;
    }

    // Parse format
    const QString format = request.queryParam("format", "json").toLower();

    // Generate report
    if (format == "csv") {
        const QString filename = QString("tag_history_%1.csv")
            .arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss"));
        const QByteArray csvData = m_reportGenerator->generateTagHistoryReport(query, "csv");
        return HttpResponse::csv(csvData, filename);
    }

    const QByteArray jsonData = m_reportGenerator->generateTagHistoryReport(query, "json");

    // Parse JSON و برگردان به عنوان QJsonObject
    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (doc.isObject()) {
        return HttpResponse::ok(doc.object());
    }

    return HttpResponse::serverError("Failed to generate report");
}

HttpResponse RestApiHandler::handleAlarmReport(const HttpRequest& request)
{
    if (!m_reportGenerator) {
        return HttpResponse::serverError("ReportGenerator not initialized");
    }

    AlarmReportQuery query;

    // Parse from
    QDateTime defaultFrom = QDateTime::currentDateTimeUtc().addDays(-1);
    query.from = parseDateTimeParam(request.queryParam("from"), defaultFrom);

    // Parse to
    QDateTime defaultTo = QDateTime::currentDateTimeUtc();
    query.to = parseDateTimeParam(request.queryParam("to"), defaultTo);

    // Parse severity
    query.severity = request.queryParam("severity");

    // Parse state
    query.state = request.queryParam("state");

    // Parse tag_id
    const QString tagIdStr = request.queryParam("tag_id");
    if (!tagIdStr.isEmpty()) {
        query.tagId = tagIdStr.toLongLong();
    }

    // Parse limit
    query.limit = request.queryParam("limit", "1000").toInt();
    if (query.limit <= 0 || query.limit > 10000) {
        query.limit = 1000;
    }

    // Parse format
    const QString format = request.queryParam("format", "json").toLower();

    // Generate report
    if (format == "csv") {
        const QString filename = QString("alarm_report_%1.csv")
            .arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss"));
        const QByteArray csvData = m_reportGenerator->generateAlarmReport(query, "csv");
        return HttpResponse::csv(csvData, filename);
    }

    const QByteArray jsonData = m_reportGenerator->generateAlarmReport(query, "json");

    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (doc.isObject()) {
        return HttpResponse::ok(doc.object());
    }

    return HttpResponse::serverError("Failed to generate report");
}

HttpResponse RestApiHandler::handleDailySummaryReport(const HttpRequest& request)
{
    if (!m_reportGenerator) {
        return HttpResponse::serverError("ReportGenerator not initialized");
    }

    DailySummaryQuery query;

    // Parse tag_ids
    const QString tagIdsStr = request.queryParam("tag_ids");
    if (tagIdsStr.isEmpty()) {
        return HttpResponse::badRequest("Missing 'tag_ids' parameter");
    }
    query.tagIds = parseTagIdsParam(tagIdsStr);

    if (query.tagIds.isEmpty()) {
        return HttpResponse::badRequest("Invalid 'tag_ids' parameter");
    }

    // Parse date
    const QString dateStr = request.queryParam("date");
    if (dateStr.isEmpty()) {
        query.date = QDate::currentDate();
    } else {
        query.date = QDate::fromString(dateStr, Qt::ISODate);
        if (!query.date.isValid()) {
            return HttpResponse::badRequest("Invalid 'date' parameter. Use YYYY-MM-DD format.");
        }
    }

    // Parse format
    const QString format = request.queryParam("format", "json").toLower();

    // Generate report
    if (format == "csv") {
        const QString filename = QString("daily_summary_%1.csv")
            .arg(query.date.toString("yyyyMMdd"));
        const QByteArray csvData = m_reportGenerator->generateDailySummaryReport(query, "csv");
        return HttpResponse::csv(csvData, filename);
    }

    const QByteArray jsonData = m_reportGenerator->generateDailySummaryReport(query, "json");

    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (doc.isObject()) {
        return HttpResponse::ok(doc.object());
    }

    return HttpResponse::serverError("Failed to generate report");
}

QVector<qint64> RestApiHandler::parseTagIdsParam(const QString& tagIdsStr) const
{
    QVector<qint64> tagIds;

    const QStringList parts = tagIdsStr.split(',', QString::SkipEmptyParts);
    for (const QString& part : parts) {
        bool ok = false;
        const qint64 id = part.trimmed().toLongLong(&ok);
        if (ok && id > 0) {
            tagIds.append(id);
        }
    }

    return tagIds;
}

QDateTime RestApiHandler::parseDateTimeParam(const QString& str, const QDateTime& defaultValue) const
{
    if (str.isEmpty()) {
        return defaultValue;
    }

    // Try ISO format with milliseconds
    QDateTime dt = QDateTime::fromString(str, Qt::ISODateWithMs);
    if (dt.isValid()) {
        return dt.toUTC();
    }

    // Try ISO format without milliseconds
    dt = QDateTime::fromString(str, Qt::ISODate);
    if (dt.isValid()) {
        return dt.toUTC();
    }

    return defaultValue;
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

