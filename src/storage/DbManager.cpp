#include "DbManager.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>
#include <QSqlRecord>
namespace
{

QVariant field(const QSqlQuery& query, const QString& name)
{
    const int index = query.record().indexOf(name);

    if (index < 0)
    {
        return QVariant();
    }

    return query.value(index);
}

QString variantToString(const QVariant& value, const QString& defaultValue)
{
    if (!value.isValid() || value.isNull())
    {
        return defaultValue;
    }

    return value.toString();
}

double variantToDouble(const QVariant& value, double defaultValue)
{
    if (!value.isValid() || value.isNull())
    {
        return defaultValue;
    }

    bool ok = false;
    const double result = value.toDouble(&ok);

    if (!ok)
    {
        return defaultValue;
    }

    return result;
}

int variantToInt(const QVariant& value, int defaultValue)
{
    if (!value.isValid() || value.isNull())
    {
        return defaultValue;
    }

    bool ok = false;
    const int result = value.toInt(&ok);

    if (!ok)
    {
        return defaultValue;
    }

    return result;
}

qint64 variantToLongLong(const QVariant& value, qint64 defaultValue)
{
    if (!value.isValid() || value.isNull())
    {
        return defaultValue;
    }

    bool ok = false;
    const qint64 result = value.toLongLong(&ok);

    if (!ok)
    {
        return defaultValue;
    }

    return result;
}

bool variantToBool(const QVariant& value, bool defaultValue)
{
    if (!value.isValid() || value.isNull())
    {
        return defaultValue;
    }

    return value.toBool();
}

} // namespace

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

        R"(
        CREATE TABLE IF NOT EXISTS system_settings (
            key TEXT PRIMARY KEY,
            value_text TEXT,
            description TEXT,
            updated_at TIMESTAMPTZ DEFAULT now()
        );
        )",

        R"(
        CREATE TABLE IF NOT EXISTS threshold_rules (
            rule_id BIGSERIAL PRIMARY KEY,
            tag_id BIGINT NOT NULL REFERENCES tags(tag_id),
            low DOUBLE PRECISION,
            high DOUBLE PRECISION,
            high_hysteresis DOUBLE PRECISION,
            low_hysteresis DOUBLE PRECISION,
            on_delay_ms INT,
            off_delay_ms INT,
            enabled BOOLEAN DEFAULT TRUE,
            created_at TIMESTAMPTZ DEFAULT now(),
            updated_at TIMESTAMPTZ DEFAULT now()
        );
        )",

        R"(
        DO $$
        BEGIN
            IF EXISTS (
                SELECT 1
                FROM information_schema.columns
                WHERE table_name = 'tags'
                  AND column_name = 'offsett'
            ) AND NOT EXISTS (
                SELECT 1
                FROM information_schema.columns
                WHERE table_name = 'tags'
                  AND column_name = 'scaling_offset'
            ) THEN
                ALTER TABLE tags RENAME COLUMN offsett TO scaling_offset;
            END IF;
        END $$;
        )",

        R"(
        ALTER TABLE tags
            ADD COLUMN IF NOT EXISTS scaling_type TEXT DEFAULT 'linear',
            ADD COLUMN IF NOT EXISTS scaling_slope DOUBLE PRECISION DEFAULT 1.0,
            ADD COLUMN IF NOT EXISTS scaling_offset DOUBLE PRECISION DEFAULT 0.0,
            ADD COLUMN IF NOT EXISTS storage_deadband DOUBLE PRECISION,
            ADD COLUMN IF NOT EXISTS alarm_hysteresis DOUBLE PRECISION,
            ADD COLUMN IF NOT EXISTS heartbeat_interval_ms INT,
            ADD COLUMN IF NOT EXISTS software_filter_type TEXT DEFAULT 'none',
            ADD COLUMN IF NOT EXISTS software_filter_config TEXT DEFAULT '{}',
            ADD COLUMN IF NOT EXISTS sim_profile TEXT DEFAULT 'sine';
        )",

        R"(
        INSERT INTO system_settings (key, value_text, description)
        VALUES
            ('engineering_decimals', '4', 'Engineering value decimal digits'),
            ('historian.batch_flush_interval_ms', '500', 'Historian flush interval'),
            ('historian.batch_max_size', '1000', 'Historian max batch size'),
            ('deadband.global_min_deadband', '0.01', 'Global minimum deadband'),
            ('deadband.default_alarm_hysteresis', '0.5', 'Default alarm hysteresis'),
            ('deadband.default_alarm_on_delay_ms', '2000', 'Default alarm on delay'),
            ('deadband.default_alarm_off_delay_ms', '2000', 'Default alarm off delay'),
            ('deadband.bad_quality_delay_ms', '3000', 'Bad quality delay'),
            ('deadband.default_heartbeat_interval_ms', '30000', 'Default storage heartbeat'),
            ('current_state.flush_interval_ms', '500', 'Current state flush interval')
        ON CONFLICT (key) DO NOTHING;
        )"

        R"(
        CREATE TABLE IF NOT EXISTS drivers (
            driver_id BIGSERIAL PRIMARY KEY,
            name TEXT UNIQUE NOT NULL,
            type TEXT NOT NULL,
            connection_config TEXT DEFAULT '{}',
            polling_interval_ms INT DEFAULT 1000,
            enabled BOOLEAN DEFAULT TRUE,
            created_at TIMESTAMPTZ DEFAULT now(),
            updated_at TIMESTAMPTZ DEFAULT now()
        );
        )",

        R"(
        ALTER TABLE tags
            ADD COLUMN IF NOT EXISTS driver_id BIGINT REFERENCES drivers(driver_id),
            ADD COLUMN IF NOT EXISTS address_config TEXT DEFAULT '{}';
        )",

        R"(
        INSERT INTO drivers (name, type, connection_config, polling_interval_ms, enabled)
        SELECT 'SIMULATOR', 'simulator', '{}', 1000, TRUE
        WHERE NOT EXISTS (
            SELECT 1 FROM drivers
        );
        )",

        R"(
        UPDATE tags
        SET driver_id = (
            SELECT driver_id
            FROM drivers
            ORDER BY driver_id
            LIMIT 1
        )
        WHERE driver_id IS NULL;
        )"


        R"(
        ALTER TABLE alarms
            ADD COLUMN IF NOT EXISTS priority TEXT DEFAULT 'medium',
            ADD COLUMN IF NOT EXISTS alarm_type TEXT DEFAULT 'threshold';
        )",

        R"(
        CREATE TABLE IF NOT EXISTS alarm_events (
            event_id BIGSERIAL PRIMARY KEY,
            alarm_id BIGINT REFERENCES alarms(alarm_id),
            event_type TEXT NOT NULL,
            event_time TIMESTAMPTZ DEFAULT now(),
            event_data TEXT,
            user_name TEXT
        );
        )",

        R"(
        CREATE INDEX IF NOT EXISTS idx_alarm_events_alarm_id
            ON alarm_events(alarm_id);
        )",

        R"(
        CREATE INDEX IF NOT EXISTS idx_alarms_tag_state
            ON alarms(tag_id, state);
        )"

        R"(
        ALTER TABLE threshold_rules
            ADD COLUMN IF NOT EXISTS lowlow DOUBLE PRECISION,
            ADD COLUMN IF NOT EXISTS highhigh DOUBLE PRECISION,
            ADD COLUMN IF NOT EXISTS lowlow_hysteresis DOUBLE PRECISION,
            ADD COLUMN IF NOT EXISTS highhigh_hysteresis DOUBLE PRECISION;
        )"
        R"(
        CREATE TABLE IF NOT EXISTS range_violation_rules (
            rule_id BIGSERIAL PRIMARY KEY,
            tag_id BIGINT NOT NULL REFERENCES tags(tag_id),
            min_value DOUBLE PRECISION,
            max_value DOUBLE PRECISION,
            severity TEXT DEFAULT 'high',
            enabled BOOLEAN DEFAULT TRUE,
            created_at TIMESTAMPTZ DEFAULT now(),
            updated_at TIMESTAMPTZ DEFAULT now()
        );
        )",

        R"(
        CREATE TABLE IF NOT EXISTS rate_of_change_rules (
            rule_id BIGSERIAL PRIMARY KEY,
            tag_id BIGINT NOT NULL REFERENCES tags(tag_id),
            max_rate_per_second DOUBLE PRECISION NOT NULL,
            window_ms INT DEFAULT 5000,
            severity TEXT DEFAULT 'high',
            enabled BOOLEAN DEFAULT TRUE,
            created_at TIMESTAMPTZ DEFAULT now(),
            updated_at TIMESTAMPTZ DEFAULT now()
        );
        )",

        R"(
        CREATE TABLE IF NOT EXISTS stuck_value_rules (
            rule_id BIGSERIAL PRIMARY KEY,
            tag_id BIGINT NOT NULL REFERENCES tags(tag_id),
            stuck_duration_ms INT DEFAULT 60000,
            epsilon DOUBLE PRECISION DEFAULT 0.01,
            severity TEXT DEFAULT 'medium',
            enabled BOOLEAN DEFAULT TRUE,
            created_at TIMESTAMPTZ DEFAULT now(),
            updated_at TIMESTAMPTZ DEFAULT now()
        );
        )",

        R"(
        CREATE TABLE IF NOT EXISTS boolean_rules (
            rule_id BIGSERIAL PRIMARY KEY,
            tag_id BIGINT NOT NULL REFERENCES tags(tag_id),
            alarm_on_true BOOLEAN DEFAULT FALSE,
            alarm_on_false BOOLEAN DEFAULT FALSE,
            duration_ms INT DEFAULT 1000,
            severity TEXT DEFAULT 'medium',
            enabled BOOLEAN DEFAULT TRUE,
            created_at TIMESTAMPTZ DEFAULT now(),
            updated_at TIMESTAMPTZ DEFAULT now()
        );
        )"

        R"(
        ALTER TABLE tags
            ADD COLUMN IF NOT EXISTS clamp_enabled BOOLEAN DEFAULT TRUE;
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
            scaling_type,
            scaling_slope,
            scaling_offset,
            deadband,
            storage_deadband,
            alarm_hysteresis,
            heartbeat_interval_ms,
            software_filter_type,
            software_filter_config,
            sim_profile,
            enabled,                  
            driver_id,
            address_config,
            updated_at,
            clamp_enabled
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
            :scaling_type,
            :scaling_slope,
            :scaling_offset,
            :deadband,
            :storage_deadband,
            :alarm_hysteresis,
            :heartbeat_interval_ms,
            :software_filter_type,
            :software_filter_config,
            :sim_profile,
            :enabled,
            :driver_id,
            :address_config,
            :clamp_enabled,
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
            scaling_type = EXCLUDED.scaling_type,
            scaling_slope = EXCLUDED.scaling_slope,
            scaling_offset = EXCLUDED.scaling_offset,
            deadband = EXCLUDED.deadband,
            storage_deadband = EXCLUDED.storage_deadband,
            alarm_hysteresis = EXCLUDED.alarm_hysteresis,
            heartbeat_interval_ms = EXCLUDED.heartbeat_interval_ms,
            software_filter_type = EXCLUDED.software_filter_type,
            software_filter_config = EXCLUDED.software_filter_config,
            sim_profile = EXCLUDED.sim_profile,
            enabled = EXCLUDED.enabled,
            driver_id =EXCLUDED.driver_id,
            address_config=EXCLUDED.address_config,
            clamp_enabled=EXCLUDED.clamp_enabled,
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
    query.bindValue(":scaling_type", tag.scalingType);
    query.bindValue(":scaling_slope", tag.slope);
    query.bindValue(":scaling_offset", tag.offset);
    query.bindValue(":deadband", tag.deadband);
    query.bindValue(":clamp_enabled", tag.clampEnabled);

    if (tag.storageDeadband >= 0.0)
    {
        query.bindValue(":storage_deadband", tag.storageDeadband);
    }
    else
    {
        query.bindValue(":storage_deadband", QVariant());
    }

    if (tag.alarmHysteresis >= 0.0)
    {
        query.bindValue(":alarm_hysteresis", tag.alarmHysteresis);
    }
    else
    {
        query.bindValue(":alarm_hysteresis", QVariant());
    }

    if (tag.heartbeatIntervalMs >= 0)
    {
        query.bindValue(":heartbeat_interval_ms", tag.heartbeatIntervalMs);
    }
    else
    {
        query.bindValue(":heartbeat_interval_ms", QVariant());
    }

    query.bindValue(":software_filter_type", tag.softwareFilter);
    query.bindValue(":software_filter_config", tag.softwareFilterConfig.isEmpty() ? "{}" : tag.softwareFilterConfig);
    query.bindValue(":sim_profile", tag.simProfile);
    query.bindValue(":enabled", tag.enabled);

    if (tag.driverId > 0)
    {
        query.bindValue(":driver_id", tag.driverId);
    }
    else
    {
        query.bindValue(":driver_id", QVariant());
    }

    query.bindValue(":address_config", tag.addressConfig.isEmpty() ? QStringLiteral("{}") : tag.addressConfig);

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

bool DbManager::writeBatch(const QVector<TagValue> &rawValues, const QVector<TagValue> &latestValues)
{
    if (rawValues.isEmpty() && latestValues.isEmpty())
        {
            return true;
        }

        if (!m_db.transaction())
        {
            qWarning() << "Cannot start database transaction:" << m_db.lastError().text();
            return false;
        }

        if (!rawValues.isEmpty())
        {
            QSqlQuery rawQuery(m_db);

            rawQuery.prepare(R"(
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

            for (const TagValue& value : rawValues)
            {
                rawQuery.bindValue(":time", value.timestamp);
                rawQuery.bindValue(":tag_id", value.tagId);
                rawQuery.bindValue(":raw_value", value.rawValue);
                rawQuery.bindValue(":eng_value", value.engineeringValue);
                rawQuery.bindValue(":quality", static_cast<int>(value.quality));
                rawQuery.bindValue(":source", sourceToString(value.source));

                if (!rawQuery.exec())
                {
                    qWarning() << "Batch raw insert failed:" << rawQuery.lastError().text();
                    m_db.rollback();
                    return false;
                }
            }
        }

        if (!latestValues.isEmpty())
        {
            QSqlQuery currentQuery(m_db);

            currentQuery.prepare(R"(
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

            for (const TagValue& value : latestValues)
            {
                currentQuery.bindValue(":tag_id", value.tagId);
                currentQuery.bindValue(":last_value", value.engineeringValue);
                currentQuery.bindValue(":last_quality", static_cast<int>(value.quality));
                currentQuery.bindValue(":last_timestamp", value.timestamp);

                if (!currentQuery.exec())
                {
                    qWarning() << "Batch current state upsert failed:" << currentQuery.lastError().text();
                    m_db.rollback();
                    return false;
                }
            }
        }

        if (!m_db.commit())
        {
            qWarning() << "Cannot commit database transaction:" << m_db.lastError().text();
            return false;
        }

        return true;

}

bool DbManager::insertRawBatch(const QVector<TagValue>& rawValues)
{
    if (rawValues.isEmpty())
    {
        return true;
    }

    if (!m_db.transaction())
    {
        qWarning() << "Cannot start database transaction:" << m_db.lastError().text();
        return false;
    }

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

    for (const TagValue& value : rawValues)
    {
        query.bindValue(":time", value.timestamp);
        query.bindValue(":tag_id", value.tagId);
        query.bindValue(":raw_value", value.rawValue);
        query.bindValue(":eng_value", value.engineeringValue);
        query.bindValue(":quality", static_cast<int>(value.quality));
        query.bindValue(":source", sourceToString(value.source));

        if (!query.exec())
        {
            qWarning() << "Batch raw insert failed:" << query.lastError().text();
            m_db.rollback();
            return false;
        }
    }

    if (!m_db.commit())
    {
        qWarning() << "Cannot commit database transaction:" << m_db.lastError().text();
        return false;
    }

    return true;
}

bool DbManager::upsertCurrentBatch(const QVector<TagValue>& latestValues)
{
    if (latestValues.isEmpty())
    {
        return true;
    }

    if (!m_db.transaction())
    {
        qWarning() << "Cannot start database transaction:" << m_db.lastError().text();
        return false;
    }

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

    for (const TagValue& value : latestValues)
    {
        query.bindValue(":tag_id", value.tagId);
        query.bindValue(":last_value", value.engineeringValue);
        query.bindValue(":last_quality", static_cast<int>(value.quality));
        query.bindValue(":last_timestamp", value.timestamp);

        if (!query.exec())
        {
            qWarning() << "Batch current state upsert failed:" << query.lastError().text();
            m_db.rollback();
            return false;
        }
    }

    if (!m_db.commit())
    {
        qWarning() << "Cannot commit database transaction:" << m_db.lastError().text();
        return false;
    }

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

int DbManager::countTags()
{
    QSqlQuery query(m_db);

    if (!query.exec("SELECT count(*) FROM tags;"))
    {
        qWarning() << "Count tags failed:" << query.lastError().text();
        return -1;
    }

    if (query.next())
    {
        return query.value(0).toInt();
    }

    return -1;
}

int DbManager::countRules()
{
    QSqlQuery query(m_db);

    if (!query.exec("SELECT count(*) FROM threshold_rules;"))
    {
        qWarning() << "Count rules failed:" << query.lastError().text();
        return -1;
    }

    if (query.next())
    {
        return query.value(0).toInt();
    }

    return -1;
}

QVector<TagDefinition> DbManager::loadTags()
{
    QVector<TagDefinition> tags;

    QSqlQuery query(m_db);

    query.prepare(R"(
        SELECT
            tag_id,
            tag_name,
            source_type,
            data_type,
            eng_units,
            raw_min,
            raw_max,
            eng_min,
            eng_max,
            scaling_type,
            scaling_slope,
            scaling_offset,
            deadband,
            storage_deadband,
            alarm_hysteresis,
            heartbeat_interval_ms,
            software_filter_type,
            software_filter_config,
            sim_profile,
            driver_id,
            address_config,
            enabled
        FROM tags
        ORDER BY tag_id;
    )");

    if (!query.exec())
    {
        qWarning() << "Load tags failed:" << query.lastError().text();
        return tags;
    }

    while (query.next())
    {
        TagDefinition tag;

        tag.tagId = variantToLongLong(field(query, "tag_id"), 0);
        tag.tagName = variantToString(field(query, "tag_name"), QString());

        tag.sourceType = variantToString(field(query, "source_type"), QStringLiteral("simulator"));
        tag.dataType = variantToString(field(query, "data_type"), QStringLiteral("float"));
        tag.engUnits = variantToString(field(query, "eng_units"), QString());

        tag.rawMin = variantToDouble(field(query, "raw_min"), 0.0);
        tag.rawMax = variantToDouble(field(query, "raw_max"), 100.0);

        tag.engMin = variantToDouble(field(query, "eng_min"), 0.0);
        tag.engMax = variantToDouble(field(query, "eng_max"), 100.0);

        tag.scalingType = variantToString(field(query, "scaling_type"), QStringLiteral("linear"));
        tag.slope = variantToDouble(field(query, "scaling_slope"), 1.0);
        tag.offset = variantToDouble(field(query, "scaling_offset"), 0.0);

        tag.deadband = variantToDouble(field(query, "deadband"), 0.0);

        tag.storageDeadband = variantToDouble(field(query, "storage_deadband"), -1.0);
        tag.alarmHysteresis = variantToDouble(field(query, "alarm_hysteresis"), -1.0);
        tag.heartbeatIntervalMs = variantToInt(field(query, "heartbeat_interval_ms"), -1);

        tag.softwareFilter = variantToString(field(query, "software_filter_type"), QStringLiteral("none"));
        tag.softwareFilterConfig = variantToString(field(query, "software_filter_config"), QStringLiteral("{}"));

        tag.simProfile = variantToString(field(query, "sim_profile"), QStringLiteral("sine"));

        tag.driverId = variantToLongLong(field(query, "driver_id"), 0);
        tag.addressConfig = variantToString(field(query, "address_config"), QStringLiteral("{}"));

        tag.enabled = variantToBool(field(query, "enabled"), true);

        tag.clampEnabled = variantToBool(field(query, "clamp_enabled"), true);
        tags.push_back(tag);
    }

    return tags;
}

QVector<ThresholdRule> DbManager::loadRules()
{
    QVector<ThresholdRule> rules;

    QSqlQuery query(m_db);

    query.prepare(R"(
        SELECT
            rule_id,
            tag_id,
            low,
            high,
            lowlow,
            highhigh,
            high_hysteresis,
            low_hysteresis,
            lowlow_hysteresis,
            highhigh_hysteresis,
            on_delay_ms,
            off_delay_ms
        FROM threshold_rules
        WHERE enabled = TRUE
        ORDER BY tag_id;
    )");

    if (!query.exec())
    {
        qWarning() << "Load rules failed:" << query.lastError().text();
        return rules;
    }

    while (query.next())
    {
        ThresholdRule rule;

        rule.ruleId = variantToLongLong(field(query, "rule_id"), 0);
        rule.tagId = variantToLongLong(field(query, "tag_id"), 0);

        const QVariant lowValue = field(query, "low");
        if (lowValue.isValid() && !lowValue.isNull())
        {
            rule.hasLow = true;
            rule.low = variantToDouble(lowValue, 0.0);
        }

        const QVariant highValue = field(query, "high");
        if (highValue.isValid() && !highValue.isNull())
        {
            rule.hasHigh = true;
            rule.high = variantToDouble(highValue, 0.0);
        }

        const QVariant lowLowValue = field(query, "lowlow");
        if (lowLowValue.isValid() && !lowLowValue.isNull())
        {
            rule.hasLowLow = true;
            rule.lowLow = variantToDouble(lowLowValue, 0.0);
        }

        const QVariant highHighValue = field(query, "highhigh");
        if (highHighValue.isValid() && !highHighValue.isNull())
        {
            rule.hasHighHigh = true;
            rule.highHigh = variantToDouble(highHighValue, 0.0);
        }

        rule.highHysteresis = variantToDouble(field(query, "high_hysteresis"), -1.0);
        rule.lowHysteresis = variantToDouble(field(query, "low_hysteresis"), -1.0);
        rule.lowLowHysteresis = variantToDouble(field(query, "lowlow_hysteresis"), -1.0);
        rule.highHighHysteresis = variantToDouble(field(query, "highhigh_hysteresis"), -1.0);

        rule.onDelayMs = variantToInt(field(query, "on_delay_ms"), -1);
        rule.offDelayMs = variantToInt(field(query, "off_delay_ms"), -1);

        rules.push_back(rule);
    }

    return rules;
}

bool DbManager::insertRule(const ThresholdRule& rule)
{
    QSqlQuery query(m_db);

    query.prepare(R"(
        INSERT INTO threshold_rules (
            tag_id,
            low,
            high,
            lowlow,
            highhigh,
            high_hysteresis,
            low_hysteresis,
            lowlow_hysteresis,
            highhigh_hysteresis,
            on_delay_ms,
            off_delay_ms,
            enabled,
            updated_at
        )
        VALUES (
            :tag_id,
            :low,
            :high,
            :lowlow,
            :highhigh,
            :high_hysteresis,
            :low_hysteresis,
            :lowlow_hysteresis,
            :highhigh_hysteresis,
            :on_delay_ms,
            :off_delay_ms,
            TRUE,
            now()
        );
    )");

    query.bindValue(":tag_id", rule.tagId);

    query.bindValue(":low", rule.hasLow ? QVariant(rule.low) : QVariant());
    query.bindValue(":high", rule.hasHigh ? QVariant(rule.high) : QVariant());
    query.bindValue(":lowlow", rule.hasLowLow ? QVariant(rule.lowLow) : QVariant());
    query.bindValue(":highhigh", rule.hasHighHigh ? QVariant(rule.highHigh) : QVariant());

    query.bindValue(":high_hysteresis", rule.highHysteresis >= 0.0 ? QVariant(rule.highHysteresis) : QVariant());
    query.bindValue(":low_hysteresis", rule.lowHysteresis >= 0.0 ? QVariant(rule.lowHysteresis) : QVariant());
    query.bindValue(":lowlow_hysteresis", rule.lowLowHysteresis >= 0.0 ? QVariant(rule.lowLowHysteresis) : QVariant());
    query.bindValue(":highhigh_hysteresis", rule.highHighHysteresis >= 0.0 ? QVariant(rule.highHighHysteresis) : QVariant());

    query.bindValue(":on_delay_ms", rule.onDelayMs >= 0 ? QVariant(rule.onDelayMs) : QVariant());
    query.bindValue(":off_delay_ms", rule.offDelayMs >= 0 ? QVariant(rule.offDelayMs) : QVariant());

    if (!query.exec())
    {
        qWarning() << "Insert rule failed:" << query.lastError().text();
        return false;
    }

    return true;
}

QString DbManager::settingValue(const QString& key, const QString& defaultValue) const
{
    QSqlQuery query(m_db);

    query.prepare("SELECT value_text FROM system_settings WHERE key = :key;");
    query.bindValue(":key", key);

    if (!query.exec())
    {
        qWarning() << "Read setting failed:" << query.lastError().text();
        return defaultValue;
    }

    if (query.next())
    {
        return query.value(0).toString();
    }

    return defaultValue;
}

bool DbManager::setSetting(const QString& key, const QString& value)
{
    QSqlQuery query(m_db);

    query.prepare(R"(
        INSERT INTO system_settings (
            key,
            value_text,
            updated_at
        )
        VALUES (
            :key,
            :value,
            now()
        )
        ON CONFLICT (key) DO UPDATE SET
            value_text = EXCLUDED.value_text,
            updated_at = now();
    )");

    query.bindValue(":key", key);
    query.bindValue(":value", value);

    if (!query.exec())
    {
        qWarning() << "Set setting failed:" << query.lastError().text();
        return false;
    }

    return true;
}

int DbManager::settingInt(const QString& key, int defaultValue) const
{
    const QString value = settingValue(key);

    if (value.isEmpty())
    {
        return defaultValue;
    }

    bool ok = false;
    const int intValue = value.toInt(&ok);

    if (!ok)
    {
        return defaultValue;
    }

    return intValue;
}

double DbManager::settingDouble(const QString& key, double defaultValue) const
{
    const QString value = settingValue(key);

    if (value.isEmpty())
    {
        return defaultValue;
    }

    bool ok = false;
    const double doubleValue = value.toDouble(&ok);

    if (!ok)
    {
        return defaultValue;
    }

    return doubleValue;
}

QVector<DriverDefinition> DbManager::loadDrivers()
{
    QVector<DriverDefinition> drivers;

    QSqlQuery query(m_db);

    query.prepare(R"(
        SELECT
            driver_id,
            name,
            type,
            connection_config,
            polling_interval_ms,
            enabled
        FROM drivers
        ORDER BY driver_id;
    )");

    if (!query.exec())
    {
        qWarning() << "Load drivers failed:" << query.lastError().text();
        return drivers;
    }

    while (query.next())
    {
        DriverDefinition driver;

        driver.driverId = variantToLongLong(field(query, "driver_id"), 0);
        driver.name = variantToString(field(query, "name"), QString());
        driver.type = variantToString(field(query, "type"), QString());
        driver.connectionConfig = variantToString(field(query, "connection_config"), QStringLiteral("{}"));
        driver.pollingIntervalMs = variantToInt(field(query, "polling_interval_ms"), 1000);
        driver.enabled = variantToBool(field(query, "enabled"), true);

        drivers.push_back(driver);
    }

    return drivers;
}

qint64 DbManager::raiseAlarm(
    qint64 tagId,
    const QString& alarmType,
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
            alarm_type,
            severity,
            priority,
            state,
            value,
            threshold,
            message,
            active_time
        )
        VALUES (
            :tag_id,
            :rule_type,
            :alarm_type,
            :severity,
            :priority,
            'active',
            :value,
            :threshold,
            :message,
            now()
        )
        RETURNING alarm_id;
    )");

    query.bindValue(":tag_id", tagId);
    query.bindValue(":rule_type", alarmType);
    query.bindValue(":alarm_type", alarmType);
    query.bindValue(":severity", severity);
    query.bindValue(":priority", severity);
    query.bindValue(":value", value);
    query.bindValue(":threshold", threshold);
    query.bindValue(":message", message);

    if (!query.exec())
    {
        qWarning() << "Raise alarm failed:" << query.lastError().text();
        return -1;
    }

    qint64 alarmId = -1;

    if (query.next())
    {
        alarmId = query.value(0).toLongLong();
    }

    if (alarmId > 0)
    {
        addAlarmEvent(alarmId, "active", message);
    }

    qInfo() << "ALARM RAISED:"
            << "tagId=" << tagId
            << "type=" << alarmType
            << "severity=" << severity
            << "value=" << value
            << "threshold=" << threshold;

    return alarmId;
}

bool DbManager::clearAlarmByTagAndType(qint64 tagId, const QString& alarmType)
{
    QSqlQuery query(m_db);

    query.prepare(R"(
        UPDATE alarms
        SET state = 'cleared',
            clear_time = now()
        WHERE tag_id = :tag_id
          AND alarm_type = :alarm_type
          AND state = 'active'
        RETURNING alarm_id;
    )");

    query.bindValue(":tag_id", tagId);
    query.bindValue(":alarm_type", alarmType);

    if (!query.exec())
    {
        qWarning() << "Clear alarm failed:" << query.lastError().text();
        return false;
    }

    while (query.next())
    {
        const qint64 alarmId = query.value(0).toLongLong();
        addAlarmEvent(alarmId, "cleared");
    }

    qInfo() << "ALARM CLEARED:"
            << "tagId=" << tagId
            << "type=" << alarmType;

    return true;
}

bool DbManager::acknowledgeAlarm(qint64 alarmId, const QString& userName)
{
    QSqlQuery query(m_db);

    query.prepare(R"(
        UPDATE alarms
        SET ack_time = now(),
            ack_user = :user_name
        WHERE alarm_id = :alarm_id;
    )");

    query.bindValue(":alarm_id", alarmId);
    query.bindValue(":user_name", userName);

    if (!query.exec())
    {
        qWarning() << "Acknowledge alarm failed:" << query.lastError().text();
        return false;
    }

    addAlarmEvent(alarmId, "acknowledged", QString(), userName);

    qInfo() << "ALARM ACKNOWLEDGED:"
            << "alarmId=" << alarmId
            << "user=" << userName;

    return true;
}

bool DbManager::addAlarmEvent(
    qint64 alarmId,
    const QString& eventType,
    const QString& eventData,
    const QString& userName
)
{
    QSqlQuery query(m_db);

    query.prepare(R"(
        INSERT INTO alarm_events (
            alarm_id,
            event_type,
            event_data,
            user_name,
            event_time
        )
        VALUES (
            :alarm_id,
            :event_type,
            :event_data,
            :user_name,
            now()
        );
    )");

    query.bindValue(":alarm_id", alarmId);
    query.bindValue(":event_type", eventType);
    query.bindValue(":event_data", eventData);
    query.bindValue(":user_name", userName);

    if (!query.exec())
    {
        qWarning() << "Add alarm event failed:" << query.lastError().text();
        return false;
    }

    return true;
}

QVector<RangeViolationRule> DbManager::loadRangeViolationRules()
{
    QVector<RangeViolationRule> rules;

    QSqlQuery query(m_db);

    query.prepare(R"(
        SELECT rule_id, tag_id, min_value, max_value, severity
        FROM range_violation_rules
        WHERE enabled = TRUE
        ORDER BY tag_id;
    )");

    if (!query.exec())
    {
        qWarning() << "Load range violation rules failed:" << query.lastError().text();
        return rules;
    }

    while (query.next())
    {
        RangeViolationRule rule;

        rule.ruleId = variantToLongLong(field(query, "rule_id"), 0);
        rule.tagId = variantToLongLong(field(query, "tag_id"), 0);
        rule.minValue = variantToDouble(field(query, "min_value"), 0.0);
        rule.maxValue = variantToDouble(field(query, "max_value"), 100.0);
        rule.severity = variantToString(field(query, "severity"), QStringLiteral("high"));

        rules.push_back(rule);
    }

    return rules;
}

QVector<RateOfChangeRule> DbManager::loadRateOfChangeRules()
{
    QVector<RateOfChangeRule> rules;

    QSqlQuery query(m_db);

    query.prepare(R"(
        SELECT rule_id, tag_id, max_rate_per_second, window_ms, severity
        FROM rate_of_change_rules
        WHERE enabled = TRUE
        ORDER BY tag_id;
    )");

    if (!query.exec())
    {
        qWarning() << "Load rate of change rules failed:" << query.lastError().text();
        return rules;
    }

    while (query.next())
    {
        RateOfChangeRule rule;

        rule.ruleId = variantToLongLong(field(query, "rule_id"), 0);
        rule.tagId = variantToLongLong(field(query, "tag_id"), 0);
        rule.maxRatePerSecond = variantToDouble(field(query, "max_rate_per_second"), 10.0);
        rule.windowMs = variantToInt(field(query, "window_ms"), 5000);
        rule.severity = variantToString(field(query, "severity"), QStringLiteral("high"));

        rules.push_back(rule);
    }

    return rules;
}

QVector<StuckValueRule> DbManager::loadStuckValueRules()
{
    QVector<StuckValueRule> rules;

    QSqlQuery query(m_db);

    query.prepare(R"(
        SELECT rule_id, tag_id, stuck_duration_ms, epsilon, severity
        FROM stuck_value_rules
        WHERE enabled = TRUE
        ORDER BY tag_id;
    )");

    if (!query.exec())
    {
        qWarning() << "Load stuck value rules failed:" << query.lastError().text();
        return rules;
    }

    while (query.next())
    {
        StuckValueRule rule;

        rule.ruleId = variantToLongLong(field(query, "rule_id"), 0);
        rule.tagId = variantToLongLong(field(query, "tag_id"), 0);
        rule.stuckDurationMs = variantToInt(field(query, "stuck_duration_ms"), 60000);
        rule.epsilon = variantToDouble(field(query, "epsilon"), 0.01);
        rule.severity = variantToString(field(query, "severity"), QStringLiteral("medium"));

        rules.push_back(rule);
    }

    return rules;
}

QVector<BooleanRule> DbManager::loadBooleanRules()
{
    QVector<BooleanRule> rules;

    QSqlQuery query(m_db);

    query.prepare(R"(
        SELECT rule_id, tag_id, alarm_on_true, alarm_on_false, duration_ms, severity
        FROM boolean_rules
        WHERE enabled = TRUE
        ORDER BY tag_id;
    )");

    if (!query.exec())
    {
        qWarning() << "Load boolean rules failed:" << query.lastError().text();
        return rules;
    }

    while (query.next())
    {
        BooleanRule rule;

        rule.ruleId = variantToLongLong(field(query, "rule_id"), 0);
        rule.tagId = variantToLongLong(field(query, "tag_id"), 0);
        rule.alarmOnTrue = variantToBool(field(query, "alarm_on_true"), false);
        rule.alarmOnFalse = variantToBool(field(query, "alarm_on_false"), false);
        rule.durationMs = variantToInt(field(query, "duration_ms"), 1000);
        rule.severity = variantToString(field(query, "severity"), QStringLiteral("medium"));

        rules.push_back(rule);
    }

    return rules;
}
