#include "CoreApplication.h"
#include "api/WebSocketServer.h"
#include "api/WebSocketHandler.h"
#include "api/HttpServer.h"
#include "api/RestApiHandler.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QStringList>
#include <QJsonArray>
#include <QJsonDocument>

QString CoreApplication::findConfigFile()
{
    const QString appDirPath = QCoreApplication::applicationDirPath();

    const QStringList candidates = {
        "config/app.json",
        "../config/app.json",
        "../../config/app.json",
        appDirPath + "/config/app.json",
        appDirPath + "/../config/app.json",
        appDirPath + "/../../config/app.json",
        appDirPath + "/../../../config/app.json"

    };

    for (const QString& candidate : candidates)
    {
        if (QFile::exists(candidate))
        {
            qInfo() << "Using config file:" << candidate;
            return candidate;
        }
    }

    qWarning() << "Config file not found in searched paths. Using default path: config/app.json";
    return "config/app.json";
}

CoreApplication::CoreApplication(QObject *parent): QObject(parent)
{

}


void CoreApplication::configureApiAuth()
{
    ApiAuthenticator::Config authConfig;

    // پیش‌فرض غیرفعال است - برای تست راحت
    authConfig.enabled = m_db.settingInt("api.auth.enabled", 0) == 1;

    // خواندن API key ها از تنظیمات (با کاما جدا شده‌اند)
    const QString keysStr = m_db.settingValue("api.auth.keys", "dev-key-123");
    authConfig.apiKeys = keysStr.split(',', QString::SkipEmptyParts);
    for (QString& key : authConfig.apiKeys) {
        key = key.trimmed();
    }

    m_restApiHandler->setAuthenticator(authConfig);

    if (authConfig.enabled) {
        qInfo() << "[API] Authentication enabled with" << authConfig.apiKeys.size() << "key(s)";
    } else {
        qInfo() << "[API] Authentication disabled (development mode)";
    }
}


bool CoreApplication::initialize()
{
    qInfo() << "Starting Tag Core...";

    const QString configPath = findConfigFile();

    auto bootstrapConfig = ConfigLoader::load(configPath);

    if (!bootstrapConfig.has_value())
    {
        qCritical() << "Failed to load bootstrap configuration";
        return false;
    }

    AppConfig bootstrap = bootstrapConfig.value();

    if (!m_db.initialize(bootstrap))
    {
        qCritical() << "Failed to initialize database";
        return false;
    }



    const QVector<DriverDefinition> initialDrivers = m_db.loadDrivers();

    qint64 defaultDriverId = 0;

    if (!initialDrivers.isEmpty())
    {
        defaultDriverId = initialDrivers.first().driverId;
    }

    qInfo() << "Initial drivers:" << initialDrivers.size()
            << "defaultDriverId:" << defaultDriverId;

    const int tagCount = m_db.countTags();

    if (tagCount < 0)
    {
        qCritical() << "Failed to read tag count from database";
        return false;
    }

    if (tagCount == 0 && !bootstrap.tags.isEmpty())
    {
        qInfo() << "Database tags are empty. Seeding tags from bootstrap file.";

        for (TagDefinition tag : bootstrap.tags)
        {
            if (tag.driverId == 0)
            {
                tag.driverId = defaultDriverId;
            }

            m_db.upsertTag(tag);
        }
    }

    const int ruleCount = m_db.countRules();

    if (ruleCount < 0)
    {
        qCritical() << "Failed to read rule count from database";
        return false;
    }

    if (ruleCount == 0 && !bootstrap.rules.isEmpty())
    {
        qInfo() << "Database rules are empty. Seeding rules from bootstrap file.";

        for (const ThresholdRule& rule : bootstrap.rules)
        {
            m_db.insertRule(rule);
        }
    }

    m_config = bootstrap;

    m_config.engineeringDecimals =
        m_db.settingInt("engineering_decimals", bootstrap.engineeringDecimals);

    m_config.batchFlushIntervalMs =
        m_db.settingInt("historian.batch_flush_interval_ms", bootstrap.batchFlushIntervalMs);

    m_config.batchMaxSize =
        m_db.settingInt("historian.batch_max_size", bootstrap.batchMaxSize);

    m_config.globalMinDeadband =
        m_db.settingDouble("deadband.global_min_deadband", bootstrap.globalMinDeadband);

    m_config.defaultAlarmHysteresis =
        m_db.settingDouble("deadband.default_alarm_hysteresis", bootstrap.defaultAlarmHysteresis);

    m_config.defaultAlarmOnDelayMs =
        m_db.settingInt("deadband.default_alarm_on_delay_ms", bootstrap.defaultAlarmOnDelayMs);

    m_config.defaultAlarmOffDelayMs =
        m_db.settingInt("deadband.default_alarm_off_delay_ms", bootstrap.defaultAlarmOffDelayMs);

    m_config.badQualityDelayMs =
        m_db.settingInt("deadband.bad_quality_delay_ms", bootstrap.badQualityDelayMs);

    m_config.defaultHeartbeatIntervalMs =
        m_db.settingInt("deadband.default_heartbeat_interval_ms", bootstrap.defaultHeartbeatIntervalMs);

    m_config.currentStateFlushIntervalMs =
        m_db.settingInt("current_state.flush_interval_ms", bootstrap.currentStateFlushIntervalMs);

    m_config.tags = m_db.loadTags();
    m_config.rules = m_db.loadRules();
    m_config.drivers = m_db.loadDrivers();

    qInfo() << "Configuration loaded from database";
    qInfo() << "Tags:" << m_config.tags.size();
    qInfo() << "Threshold rules:" << m_config.rules.size();
    qInfo() << "Drivers:" << m_config.drivers.size();

    const QString dashboardsPath = QCoreApplication::applicationDirPath() + "/dashboards";
    m_dashboardManager = std::make_unique<DashboardManager>(m_db, dashboardsPath);

    m_historianWriter = std::make_unique<BatchHistorianWriter>(
        m_db,
        m_config.batchFlushIntervalMs,
        m_config.batchMaxSize
    );

    m_currentStateWriter = std::make_unique<CurrentStateWriter>(
        m_bus,
        m_db,
        m_config.currentStateFlushIntervalMs
    );

    m_storageFilter = std::make_unique<StorageExceptionFilter>(
        m_bus,
        *m_historianWriter,
        m_config
    );

    m_realtimeCache = std::make_unique<RealtimeCache>(m_bus);

    m_ruleEngine = std::make_unique<RuleEngine>(
        m_bus,
        m_db,
        m_config
    );

    m_notificationManager = std::make_unique<NotificationManager>(
        m_bus,
        m_db,
        m_config
    );

    m_filterProcessor = std::make_unique<FilterProcessor>(m_bus, m_config);

    qInfo() << "Starting DriverManager...";

    m_driverManager = std::make_unique<DriverManager>(m_bus, m_config);

    if (!m_driverManager->startAll())
    {
        qCritical() << "Failed to start DriverManager";
        return false;
    }

    m_config.rangeViolationRules = m_db.loadRangeViolationRules();
    m_config.rateOfChangeRules = m_db.loadRateOfChangeRules();
    m_config.stuckValueRules = m_db.loadStuckValueRules();
    m_config.booleanRules = m_db.loadBooleanRules();

    qInfo() << "Range Violation rules:" << m_config.rangeViolationRules.size();
    qInfo() << "Rate of Change rules:" << m_config.rateOfChangeRules.size();
    qInfo() << "Stuck Value rules:" << m_config.stuckValueRules.size();
    qInfo() << "Boolean rules:" << m_config.booleanRules.size();
    qInfo() << "Tag Core initialized successfully";


    m_config.notificationRules = m_db.loadNotificationRules();

    qInfo() << "Notification rules:" << m_config.notificationRules.size();


    m_computedTagEngine = std::make_unique<ComputedTagEngine>(
        m_bus,
        m_db,
        m_config
    );

    m_historianManager = std::make_unique<HistorianManager>(m_db);

    //  only for write test
    QTimer::singleShot(10000, [this]()
    {
        TagValue writeCommand;
        writeCommand.tagId = 1002;
        writeCommand.engineeringValue = 55.0;

        m_bus.publish("commands/1002/write", writeCommand);
    });

    m_config.computedTags = m_db.loadComputedTags();

    qInfo() << "Computed tags:" << m_config.computedTags.size();
    const QString archivePath = "C:/tag_archive";
    const qint64 maxArchiveSizeBytes = 100LL * 1024LL * 1024LL;  // 100 MB
    const int archiveCheckIntervalMs = 3600000;  // 1 ساعت

    m_archiveManager = std::make_unique<ArchiveManager>(
        m_db,
        archivePath,
        maxArchiveSizeBytes,
        archiveCheckIntervalMs
    );
    startApiLayer();
    return true;
}

void CoreApplication::startApiLayer()
{
    m_wsServer = new WebSocketServer(this);

    WebSocketServer::Config config;
    config.enabled = true;
    config.host = QStringLiteral("0.0.0.0");
    config.port = 8081;
    config.batchIntervalMs = 100;

    QObject::connect(m_wsServer, &WebSocketServer::started,
            this, [](const QString &host, int port) {
        qInfo() << "[API] WebSocket server started on" << host << ":" << port;
    });

    QObject::connect(m_wsServer, &WebSocketServer::failed,
            this, [](const QString &message) {
        qWarning() << "[API] WebSocket server failed:" << message;
    });

    if (!m_wsServer->start(config)) {
        qWarning() << "[API] Failed to start WebSocket server";
        return;
    }

    // ✅ Snapshot Provider - فقط یک بار
    m_wsServer->handler()->setSnapshotProvider(
        [this](const QVector<int>& tagIds) -> QVector<QJsonObject> {
            return m_db.getTagsCurrentState(tagIds);
        }
    );

    // Bridge TagBus → WebSocketHandler
    m_bus.subscribe("tags/#", [this](const BusMessage& msg) {
        if (!msg.topic.endsWith("/update")) return;
        if (m_wsServer && m_wsServer->handler()) {
            m_wsServer->handler()->publishTagUpdate(msg.value);
        }
    });

    m_bus.subscribe("alarms/#", [this](const BusMessage& msg) {
        if (m_wsServer && m_wsServer->handler()) {
            m_wsServer->handler()->publishAlarmEvent(msg.value);
        }
    });

    // ✅ Command Handler
    setupCommandHandler();

    // ✅ REST API Server
    m_httpServer = new HttpServer(this);
    m_restApiHandler = new RestApiHandler(m_db, m_dashboardManager.get(), this);
    configureApiAuth();

    m_httpServer->setRequestHandler(
        [this](const HttpRequest& request) -> HttpResponse {
            return m_restApiHandler->handleRequest(request);
        }
    );


    QObject::connect(m_httpServer, &HttpServer::started,
                     this, [](const QString& host, int port) {
        qInfo() << "[API] REST API server started on" << host << ":" << port;
    });

    QObject::connect(m_httpServer, &HttpServer::failed,
                     this, [](const QString& message) {
        qWarning() << "[API] REST API server failed:" << message;
    });

    if (!m_httpServer->start(config.host, 8082)) {
        qWarning() << "[API] Failed to start REST API server";
        return;
    }


    qInfo() << "[API] API Layer started";
}


void CoreApplication::setupCommandHandler()
{
    if (!m_wsServer || !m_wsServer->handler()) {
        return;
    }

    m_wsServer->handler()->setCommandHandler(
        [this](const QString& op, const QJsonObject& payload) -> QJsonObject {

            if (op == "write_tag") {
                return handleWriteTagCommand(payload);
            }

            if (op == "ack_alarm") {
                return handleAckAlarmCommand(payload);
            }

            if (op == "get_current") {
                return handleGetCurrentCommand(payload);
            }

            if (op == "reload_drivers") {
                return handleReloadDriversCommand(payload);
            }

            if (op == "start_driver") {
                return handleStartDriverCommand(payload);
            }


            if (op == "stop_driver") {
                return handleStopDriverCommand(payload);
            }

            if (op == "list_drivers") {
                return handleListDriversCommand(payload);
            }

            if (op == "load_dashboard") {
                return handleLoadDashboardCommand(payload);
            }

            QJsonObject result;
            result.insert("ok", false);
            result.insert("error", "Unknown command: " + op);
            return result;
        }
    );

    qInfo() << "[API] Command handler configured";
}

QJsonObject CoreApplication::handleStartDriverCommand(const QJsonObject& payload)
{
    QJsonObject result;

    if (!m_driverManager) {
        result.insert("ok", false);
        result.insert("error", "DriverManager not initialized");
        return result;
    }

    // اگر driver_id مشخص نشده، همه درایورها را start کن
    if (!payload.contains("driver_id")) {
        if (m_driverManager->startAll()) {
            result.insert("ok", true);
            QJsonObject data;
            data.insert("started", "all");
            result.insert("data", data);
            qInfo() << "All drivers started";
        } else {
            result.insert("ok", false);
            result.insert("error", "Failed to start all drivers");
        }
        return result;
    }

    const qint64 driverId = payload.value("driver_id").toVariant().toLongLong();

    if (m_driverManager->startDriver(driverId)) {
        result.insert("ok", true);
        QJsonObject data;
        data.insert("started", driverId);
        result.insert("data", data);
    } else {
        result.insert("ok", false);
        result.insert("error", "Failed to start driver: " + QString::number(driverId));
    }

    return result;
}

QJsonObject CoreApplication::handleStopDriverCommand(const QJsonObject& payload)
{
    QJsonObject result;

    if (!m_driverManager) {
        result.insert("ok", false);
        result.insert("error", "DriverManager not initialized");
        return result;
    }

    // اگر driver_id مشخص نشده، همه درایورها را stop کن
    if (!payload.contains("driver_id")) {
        m_driverManager->stopAll();
        result.insert("ok", true);
        QJsonObject data;
        data.insert("stopped", "all");
        result.insert("data", data);
        qInfo() << "All drivers stopped";
        return result;
    }

    const qint64 driverId = payload.value("driver_id").toVariant().toLongLong();

    if (m_driverManager->stopDriver(driverId)) {
        result.insert("ok", true);
        QJsonObject data;
        data.insert("stopped", driverId);
        result.insert("data", data);
    } else {
        result.insert("ok", false);
        result.insert("error", "Failed to stop driver: " + QString::number(driverId));
    }

    return result;
}


QJsonObject CoreApplication::handleWriteTagCommand(const QJsonObject& payload)
{
    QJsonObject result;

    // بررسی فیلدهای اجباری
    if (!payload.contains("tag_id")) {
        result.insert("ok", false);
        result.insert("error", "Missing 'tag_id' field");
        return result;
    }

    if (!payload.contains("value")) {
        result.insert("ok", false);
        result.insert("error", "Missing 'value' field");
        return result;
    }

    const qint64 tagId = payload.value("tag_id").toVariant().toLongLong();
    const double value = payload.value("value").toDouble();

    // بررسی وجود تگ
    bool tagExists = false;
    const QVector<TagDefinition> tags = m_db.loadTags();
    for (const TagDefinition& tag : tags) {
        if (tag.tagId == tagId) {
            tagExists = true;
            break;
        }
    }

    if (!tagExists) {
        result.insert("ok", false);
        result.insert("error", "Tag not found: " + QString::number(tagId));
        return result;
    }

    // ساخت TagValue برای publish
    TagValue writeCommand;
    writeCommand.tagId = tagId;
    writeCommand.engineeringValue = value;
    writeCommand.timestamp = QDateTime::currentDateTimeUtc();
    writeCommand.quality = Quality::Good;
    writeCommand.source = SourceKind::Manual;

    // Publish به TagBus روی topic commands/{tag_id}/write
    const QString topic = QString("commands/%1/write").arg(tagId);
    m_bus.publish(topic, writeCommand);

    qInfo() << "Write command published:" << topic << "value=" << value;

    result.insert("ok", true);
    QJsonObject data;
    data.insert("tag_id", tagId);
    data.insert("written_value", value);
    result.insert("data", data);

    return result;
}

QJsonObject CoreApplication::handleAckAlarmCommand(const QJsonObject& payload)
{
    QJsonObject result;

    if (!payload.contains("alarm_id")) {
        result.insert("ok", false);
        result.insert("error", "Missing 'alarm_id' field");
        return result;
    }

    const qint64 alarmId = payload.value("alarm_id").toVariant().toLongLong();
    QString userName = "system";
    if (payload.contains("user_name")) {
        userName = payload.value("user_name").toString();
    }

    if (m_db.acknowledgeAlarm(alarmId, userName)) {
        result.insert("ok", true);
        QJsonObject data;
        data.insert("alarm_id", alarmId);
        data.insert("acknowledged", true);
        data.insert("user_name", userName);
        result.insert("data", data);

        // Publish یک event برای اطلاع بقیه client ها
        TagValue ackEvent;
        ackEvent.tagId = alarmId;
        ackEvent.tagName = "alarm_acknowledged";
        ackEvent.timestamp = QDateTime::currentDateTimeUtc();
        ackEvent.quality = Quality::Good;
        ackEvent.source = SourceKind::Manual;
        m_bus.publish("alarms/" + QString::number(alarmId) + "/acknowledged", ackEvent);
    } else {
        result.insert("ok", false);
        result.insert("error", "Failed to acknowledge alarm");
    }

    return result;
}

QJsonObject CoreApplication::handleGetCurrentCommand(const QJsonObject& payload)
{
    QJsonObject result;

    if (!payload.contains("tag_ids")) {
        result.insert("ok", false);
        result.insert("error", "Missing 'tag_ids' field");
        return result;
    }

    // تبدیل tag_ids به QVector<int>
    QVector<int> tagIds;
    const QJsonArray idsArray = payload.value("tag_ids").toArray();
    for (const QJsonValue& v : idsArray) {
        tagIds.append(v.toVariant().toInt());
    }

    if (tagIds.isEmpty()) {
        result.insert("ok", false);
        result.insert("error", "tag_ids is empty");
        return result;
    }

    // گرفتن مقادیر فعلی از دیتابیس
    const QVector<QJsonObject> currentValues = m_db.getTagsCurrentState(tagIds);

    result.insert("ok", true);
    QJsonObject data;
    QJsonArray tagsArray;
    for (const QJsonObject& cv : currentValues) {
        tagsArray.append(cv);
    }
    data.insert("tags", tagsArray);
    data.insert("count", tagsArray.size());
    result.insert("data", data);

    return result;
}

QJsonObject CoreApplication::handleReloadDriversCommand(const QJsonObject& payload)
{
    Q_UNUSED(payload)

    QJsonObject result;

    if (!m_driverManager) {
        result.insert("ok", false);
        result.insert("error", "DriverManager not initialized");
        return result;
    }

    // توقف درایورها
    m_driverManager->stopAll();

    // بارگذاری مجدد config
    m_config.tags = m_db.loadTags();
    m_config.drivers = m_db.loadDrivers();

    // شروع مجدد درایورها
    if (m_driverManager->startAll()) {
        result.insert("ok", true);
        QJsonObject data;
        data.insert("reloaded", true);
        data.insert("tags_count", m_config.tags.size());
        data.insert("drivers_count", m_config.drivers.size());
        result.insert("data", data);

        qInfo() << "Drivers reloaded. Tags:" << m_config.tags.size()
                << "Drivers:" << m_config.drivers.size();
    } else {
        result.insert("ok", false);
        result.insert("error", "Failed to restart drivers");
    }

    return result;
}

QJsonObject CoreApplication::handleListDriversCommand(const QJsonObject& payload)
{
    Q_UNUSED(payload)

    QJsonObject result;

    if (!m_driverManager) {
        result.insert("ok", false);
        result.insert("error", "DriverManager not initialized");
        return result;
    }

    const QVector<DriverDefinition> drivers = m_db.loadDrivers();
    const QVector<qint64> runningIds = m_driverManager->runningDriverIds();

    QJsonArray driversArray;
    for (const DriverDefinition& driver : drivers) {
        QJsonObject driverObj;
        driverObj.insert("driver_id", driver.driverId);
        driverObj.insert("name", driver.name);
        driverObj.insert("type", driver.type);
        driverObj.insert("enabled", driver.enabled);
        driverObj.insert("running", runningIds.contains(driver.driverId));
        driversArray.append(driverObj);
    }

    result.insert("ok", true);
    QJsonObject data;
    data.insert("drivers", driversArray);
    data.insert("count", driversArray.size());
    result.insert("data", data);

    return result;
}

QJsonObject CoreApplication::handleLoadDashboardCommand(const QJsonObject& payload)
{
    QJsonObject result;

    if (!payload.contains("dashboard_id")) {
        result.insert("ok", false);
        result.insert("error", "Missing 'dashboard_id' field");
        return result;
    }

    const qint64 dashboardId = payload.value("dashboard_id").toVariant().toLongLong();
    const DashboardDefinition dashboard = m_db.loadDashboard(dashboardId);

    if (dashboard.dashboardId == 0) {
        result.insert("ok", false);
        result.insert("error", "Dashboard not found");
        return result;
    }

    // Parse config برای استخراج tag_ids
    QJsonArray tagIdsArray;
    QJsonDocument configDoc = QJsonDocument::fromJson(dashboard.config.toUtf8());
    if (configDoc.isObject()) {
        QJsonObject configObj = configDoc.object();
        if (configObj.contains("tag_ids")) {
            tagIdsArray = configObj.value("tag_ids").toArray();
        }
    }

    result.insert("ok", true);
    QJsonObject data;
    data.insert("dashboard_id", dashboard.dashboardId);
    data.insert("name", dashboard.name);
    data.insert("description", dashboard.description);
    data.insert("tag_ids", tagIdsArray);

    // config کامل را هم برگردان
    if (configDoc.isObject()) {
        data.insert("config", configDoc.object());
    }

    result.insert("data", data);

    return result;
}


