#include "CoreApplication.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QStringList>

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

    return true;
}
