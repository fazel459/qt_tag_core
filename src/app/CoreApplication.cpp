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

    auto config = ConfigLoader::load(configPath);

    if (!config.has_value())
    {
        qCritical() << "Failed to load configuration";
        return false;
    }

    m_config = config.value();

    if (!m_db.initialize(m_config))
    {
        qCritical() << "Failed to initialize database";
        return false;
    }

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
    m_simulatorDriver = std::make_unique<SimulatorDriver>(
        m_bus,
        m_config.tags,
        m_config.engineeringDecimals
    );

    m_simulatorDriver->start();

    qInfo() << "Tag Core initialized successfully";
    qInfo() << "Tags:" << m_config.tags.size();
    qInfo() << "Threshold rules:" << m_config.rules.size();

    return true;
}
