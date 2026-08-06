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

    // اگر دیتابیس خالی بود، از فایل bootstrap/app.json seed می‌کنیم.
    const int tagCount = m_db.countTags();

    if (tagCount < 0)
    {
        qCritical() << "Failed to read tag count from database";
        return false;
    }

    if (tagCount == 0 && !bootstrap.tags.isEmpty())
    {
        qInfo() << "Database tags are empty. Seeding tags from bootstrap file.";

        for (const TagDefinition& tag : bootstrap.tags)
        {
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

    // حالا کانفیگ نهایی را از دیتابیس می‌خوانیم.
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

    qInfo() << "Configuration loaded from database";
    qInfo() << "Tags:" << m_config.tags.size();
    qInfo() << "Threshold rules:" << m_config.rules.size();

    for (const TagDefinition& tag : m_config.tags)
    {
        m_db.upsertTag(tag);
    }

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
        m_config.rules,
        m_config
    );

    m_filterProcessor = std::make_unique<FilterProcessor>(m_bus, m_config);

    m_simulatorDriver = std::make_unique<SimulatorDriver>(
        m_bus,
        m_config.tags

    );

    m_simulatorDriver->start();

    qInfo() << "Tag Core initialized successfully";

    return true;
}
