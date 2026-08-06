#include "DbManager.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

bool DbManager::initialize(const AppConfig& cfg)
{
    const QStringList availableDrivers = QSqlDatabase::drivers();

    qInfo() << "Available Qt SQL drivers:" << availableDrivers;

    QString driverName;

    if (cfg.dbDriver.compare("auto", Qt::CaseInsensitive) == 0)
    {
        if (availableDrivers.contains("QPSQL"))
        {
            driverName = "QPSQL";
        }
        else if (availableDrivers.contains("QODBC"))
        {
            driverName = "QODBC";
        }
        else
        {
            qCritical() << "No suitable SQL driver found. Need QPSQL or QODBC.";
            return false;
        }
    }
    else
    {
        driverName = cfg.dbDriver;

        if (!availableDrivers.contains(driverName))
        {
            qCritical() << "Requested SQL driver is not available:" << driverName;
            return false;
        }
    }

    qInfo() << "Using SQL driver:" << driverName;

    m_db = QSqlDatabase::addDatabase(driverName);

    if (driverName == "QPSQL")
    {
        m_db.setHostName(cfg.dbHost);
        m_db.setPort(cfg.dbPort);
        m_db.setDatabaseName(cfg.dbName);
        m_db.setUserName(cfg.dbUser);
        m_db.setPassword(cfg.dbPassword);
    }
    else if (driverName == "QODBC")
    {
        const QString connectionString =
            QStringLiteral("Driver={%1};"
                           "Server=%2;"
                           "Port=%3;"
                           "Database=%4;"
                           "Uid=%5;"
                           "Pwd=%6;")
                .arg(cfg.odbcDriver)
                .arg(cfg.dbHost)
                .arg(cfg.dbPort)
                .arg(cfg.dbName)
                .arg(cfg.dbUser)
                .arg(cfg.dbPassword);

        qInfo() << "ODBC connection string:" << connectionString;

        m_db.setDatabaseName(connectionString);
    }
    else
    {
        qCritical() << "Unsupported SQL driver for this application:" << driverName;
        return false;
    }

    if (!m_db.open())
    {
        qCritical() << "Cannot open database:" << m_db.lastError().text();
        return false;
    }

    if (!migrate())
    {
        qCritical() << "Database migration failed";
        return false;
    }

    qInfo() << "Connected to database:" << cfg.dbName;
    return true;
}

bool DbManager::migrate()
{
    const QStringList statements = {
        "CREATE EXTENSION IF NOT EXISTS timescaledb;",

        R"(
        CREATE TABLE IF NOT EXISTS tags (
            tag_id BIGINT PRIMARY KEY,
            tag_name TEXT UNIQUE NOT NULL,
            source_type TEXT,
            data_type TEXT,
            eng_units TEXT,
            raw_min DOUBLE PRECISION,
            raw_max DOUBLE PRECISION,
            eng_min DOUBLE PRECISION,
            eng_max DOUBLE PRECISION,
            deadband DOUBLE PRECISION,
            enabled BOOLEAN DEFAULT TRUE,
            created_at TIMESTAMPTZ DEFAULT now(),
            updated_at TIMESTAMPTZ DEFAULT now()
        );
        )",

        R"(
        CREATE TABLE IF NOT EXISTS tag_values_raw (
            time TIMESTAMPTZ NOT NULL,
            tag_id BIGINT NOT NULL,
            raw_value DOUBLE PRECISION,
            eng_value DOUBLE PRECISION,
            quality INT,
            source TEXT
        );
        )",

        "CREATE INDEX IF NOT EXISTS idx_tag_values_raw_tag_time ON tag_values_raw (tag_id, time DESC);",

        "SELECT create_hypertable('tag_values_raw', 'time', if_not_exists => TRUE);",

        R"(
        CREATE TABLE IF NOT EXISTS tag_current_state (
            tag_id BIGINT PRIMARY KEY,
            last_value DOUBLE PRECISION,
            last_quality INT,
            last_timestamp TIMESTAMPTZ,
            updated_at TIMESTAMPTZ DEFAULT now()
        );
        )",

        R"(
        CREATE TABLE IF NOT EXISTS alarms (
            alarm_id BIGSERIAL PRIMARY KEY,
            tag_id BIGINT,
            rule_type TEXT,
            severity TEXT,
            state TEXT,
            value DOUBLE PRECISION,
            threshold DOUBLE PRECISION,
            message TEXT,
            active_time TIMESTAMPTZ DEFAULT now(),
            clear_time TIMESTAMPTZ,
            ack_time TIMESTAMPTZ,
            ack_user TEXT
        );
        )"
    };

    for (const QString& sql : statements)
    {
        QSqlQuery query(m_db);

        if (!query.exec(sql))
        {
            qWarning() << "Migration statement failed:" << query.lastError().text();
            qWarning() << "SQL:" << sql;
        }
    }

    return true;
}

bool DbManager::upsertTag(const TagDefinition& tag)
{
    QSqlQuery query(m_db);

    query.prepare(R"(
        INSERT INTO tags (
            tag_id,
            tag_name,
            source_type,
            data_type,
            eng_units,
            raw_min,
            raw_max,
            eng_min,
            eng_max,
            deadband,
            enabled,
            updated_at
        )
        VALUES (
            :tag_id,
            :tag_name,
            :source_type,
            :data_type,
            :eng_units,
            :raw_min,
            :raw_max,
            :eng_min,
            :eng_max,
            :deadband,
            :enabled,
            now()
        )
        ON CONFLICT (tag_id) DO UPDATE SET
            tag_name = EXCLUDED.tag_name,
            source_type = EXCLUDED.source_type,
            data_type = EXCLUDED.data_type,
            eng_units = EXCLUDED.eng_units,
            raw_min = EXCLUDED.raw_min,
            raw_max = EXCLUDED.raw_max,
            eng_min = EXCLUDED.eng_min,
            eng_max = EXCLUDED.eng_max,
            deadband = EXCLUDED.deadband,
            enabled = EXCLUDED.enabled,
            updated_at = now();
    )");

    query.bindValue(":tag_id", tag.tagId);
    query.bindValue(":tag_name", tag.tagName);
    query.bindValue(":source_type", tag.sourceType);
    query.bindValue(":data_type", tag.dataType);
    query.bindValue(":eng_units", tag.engUnits);
    query.bindValue(":raw_min", tag.rawMin);
    query.bindValue(":raw_max", tag.rawMax);
    query.bindValue(":eng_min", tag.engMin);
    query.bindValue(":eng_max", tag.engMax);
    query.bindValue(":deadband", tag.deadband);
    query.bindValue(":enabled", tag.enabled);

    if (!query.exec())
    {
        qWarning() << "Upsert tag failed:" << query.lastError().text();
        return false;
    }

    return true;
}

bool DbManager::insertRaw(const TagValue& value)
{
    QSqlQuery query(m_db);

    query.prepare(R"(
        INSERT INTO tag_values_raw (
            time,
            tag_id,
            raw_value,
            eng_value,
            quality,
            source
        )
        VALUES (
            :time,
            :tag_id,
            :raw_value,
            :eng_value,
            :quality,
            :source
        );
    )");

    query.bindValue(":time", value.timestamp);
    query.bindValue(":tag_id", value.tagId);
    query.bindValue(":raw_value", value.rawValue);
    query.bindValue(":eng_value", value.engineeringValue);
    query.bindValue(":quality", static_cast<int>(value.quality));
    query.bindValue(":source", sourceToString(value.source));

    if (!query.exec())
    {
        qWarning() << "Insert raw value failed:" << query.lastError().text();
        return false;
    }

    return true;
}

bool DbManager::upsertCurrent(const TagValue& value)
{
    QSqlQuery query(m_db);

    query.prepare(R"(
        INSERT INTO tag_current_state (
            tag_id,
            last_value,
            last_quality,
            last_timestamp,
            updated_at
        )
        VALUES (
            :tag_id,
            :last_value,
            :last_quality,
            :last_timestamp,
            now()
        )
        ON CONFLICT (tag_id) DO UPDATE SET
            last_value = EXCLUDED.last_value,
            last_quality = EXCLUDED.last_quality,
            last_timestamp = EXCLUDED.last_timestamp,
            updated_at = now();
    )");

    query.bindValue(":tag_id", value.tagId);
    query.bindValue(":last_value", value.engineeringValue);
    query.bindValue(":last_quality", static_cast<int>(value.quality));
    query.bindValue(":last_timestamp", value.timestamp);

    if (!query.exec())
    {
        qWarning() << "Upsert current state failed:" << query.lastError().text();
        return false;
    }

    return true;
}

bool DbManager::insertAlarm(
    qint64 tagId,
    const QString& ruleType,
    const QString& severity,
    double value,
    double threshold,
    const QString& message
)
{
    QSqlQuery query(m_db);

    query.prepare(R"(
        INSERT INTO alarms (
            tag_id,
            rule_type,
            severity,
            state,
            value,
            threshold,
            message,
            active_time
        )
        VALUES (
            :tag_id,
            :rule_type,
            :severity,
            'active',
            :value,
            :threshold,
            :message,
            now()
        );
    )");

    query.bindValue(":tag_id", tagId);
    query.bindValue(":rule_type", ruleType);
    query.bindValue(":severity", severity);
    query.bindValue(":value", value);
    query.bindValue(":threshold", threshold);
    query.bindValue(":message", message);

    if (!query.exec())
    {
        qWarning() << "Insert alarm failed:" << query.lastError().text();
        return false;
    }

    qInfo() << "ALARM ACTIVE:"
            << "tagId=" << tagId
            << "rule=" << ruleType
            << "severity=" << severity
            << "value=" << value
            << "threshold=" << threshold
            << "message=" << message;

    return true;
}

bool DbManager::clearActiveAlarms(qint64 tagId, const QString& ruleType)
{
    QSqlQuery query(m_db);

    query.prepare(R"(
        UPDATE alarms
        SET state = 'cleared',
            clear_time = now()
        WHERE tag_id = :tag_id
          AND rule_type = :rule_type
          AND state = 'active';
    )");

    query.bindValue(":tag_id", tagId);
    query.bindValue(":rule_type", ruleType);

    if (!query.exec())
    {
        qWarning() << "Clear alarms failed:" << query.lastError().text();
        return false;
    }

    qInfo() << "ALARM CLEARED:"
            << "tagId=" << tagId
            << "rule=" << ruleType;

    return true;
}

QString DbManager::sourceToString(SourceKind source)
{
    switch (source)
    {
        case SourceKind::RealDriver:
            return "real_driver";

        case SourceKind::Simulator:
            return "simulator";

        case SourceKind::Calculated:
            return "calculated";

        case SourceKind::Manual:
            return "manual";

        case SourceKind::Replay:
            return "replay";
    }

    return "unknown";
}