#include "DbManager.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>
#include <QSqlRecord>
namespace
{

static bool execSql(QSqlDatabase& db, const QString& sql, const char* what)
{
    QSqlQuery q(db);
    if (!q.exec(sql)) {
        qWarning() << "Migration failed:" << what << "-" << q.lastError().text();
        return false;
    }
    return true;
}

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

    migrateApiTables();   // ✅ جدول‌های API
    seedDefaults();       // ✅ داده‌های اولیه

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

        R"(
        CREATE TABLE IF NOT EXISTS notification_rules (
            notification_rule_id BIGSERIAL PRIMARY KEY,
            name TEXT NOT NULL,
            severity_filter TEXT,
            alarm_type_filter TEXT,
            channel TEXT NOT NULL,
            channel_config TEXT DEFAULT '{}',
            throttle_ms INT DEFAULT 60000,
            enabled BOOLEAN DEFAULT TRUE,
            created_at TIMESTAMPTZ DEFAULT now(),
            updated_at TIMESTAMPTZ DEFAULT now()
        );
        )",

        R"(
        CREATE TABLE IF NOT EXISTS notification_log (
            notification_id BIGSERIAL PRIMARY KEY,
            alarm_id BIGINT,
            notification_rule_id BIGINT,
            channel TEXT,
            status TEXT,
            message TEXT,
            sent_at TIMESTAMPTZ DEFAULT now()
        );
        )",

        R"(
        CREATE INDEX IF NOT EXISTS idx_notification_log_alarm
            ON notification_log(alarm_id);
        )"
        R"(
        CREATE TABLE IF NOT EXISTS computed_tags (
            computed_tag_id BIGSERIAL PRIMARY KEY,
            tag_id BIGINT NOT NULL REFERENCES tags(tag_id),
            expression TEXT NOT NULL,
            update_mode TEXT DEFAULT 'on_change',
            update_interval_ms INT DEFAULT 1000,
            enabled BOOLEAN DEFAULT TRUE,
            created_at TIMESTAMPTZ DEFAULT now(),
            updated_at TIMESTAMPTZ DEFAULT now()
        );
        )",

        R"(
        CREATE INDEX IF NOT EXISTS idx_computed_tags_tag
            ON computed_tags(tag_id);
        )"
        R"(
        CREATE MATERIALIZED VIEW IF NOT EXISTS tag_values_1min
        WITH (timescaledb.continuous) AS
        SELECT
            time_bucket(INTERVAL '1 minute', time) AS bucket,
            tag_id,
            AVG(eng_value) AS avg_value,
            MIN(eng_value) AS min_value,
            MAX(eng_value) AS max_value,
            FIRST(eng_value, time) AS first_value,
            LAST(eng_value, time) AS last_value,
            COUNT(*) AS sample_count
        FROM tag_values_raw
        GROUP BY bucket, tag_id
        WITH NO DATA;
        )",

        R"(
        CREATE MATERIALIZED VIEW IF NOT EXISTS tag_values_5min
        WITH (timescaledb.continuous) AS
        SELECT
            time_bucket(INTERVAL '5 minutes', time) AS bucket,
            tag_id,
            AVG(eng_value) AS avg_value,
            MIN(eng_value) AS min_value,
            MAX(eng_value) AS max_value,
            FIRST(eng_value, time) AS first_value,
            LAST(eng_value, time) AS last_value,
            COUNT(*) AS sample_count
        FROM tag_values_raw
        GROUP BY bucket, tag_id
        WITH NO DATA;
        )",

        R"(
        CREATE MATERIALIZED VIEW IF NOT EXISTS tag_values_1hour
        WITH (timescaledb.continuous) AS
        SELECT
            time_bucket(INTERVAL '1 hour', time) AS bucket,
            tag_id,
            AVG(eng_value) AS avg_value,
            MIN(eng_value) AS min_value,
            MAX(eng_value) AS max_value,
            FIRST(eng_value, time) AS first_value,
            LAST(eng_value, time) AS last_value,
            COUNT(*) AS sample_count
        FROM tag_values_raw
        GROUP BY bucket, tag_id
        WITH NO DATA;
        )",

        R"(
        CREATE MATERIALIZED VIEW IF NOT EXISTS tag_values_1day
        WITH (timescaledb.continuous) AS
        SELECT
            time_bucket(INTERVAL '1 day', time) AS bucket,
            tag_id,
            AVG(eng_value) AS avg_value,
            MIN(eng_value) AS min_value,
            MAX(eng_value) AS max_value,
            FIRST(eng_value, time) AS first_value,
            LAST(eng_value, time) AS last_value,
            COUNT(*) AS sample_count
        FROM tag_values_raw
        GROUP BY bucket, tag_id
        WITH NO DATA;
        )"
        R"(
        SELECT add_continuous_aggregate_policy('tag_values_1min',
            start_offset => INTERVAL '3 minutes',
            end_offset => INTERVAL '1 minute',
            schedule_interval => INTERVAL '1 minute',
            if_not_exists => TRUE
        );
        )",

        R"(
        SELECT add_continuous_aggregate_policy('tag_values_5min',
            start_offset => INTERVAL '15 minutes',
            end_offset => INTERVAL '5 minutes',
            schedule_interval => INTERVAL '5 minutes',
            if_not_exists => TRUE
        );
        )",

        R"(
        SELECT add_continuous_aggregate_policy('tag_values_1hour',
            start_offset => INTERVAL '3 hours',
            end_offset => INTERVAL '1 hour',
            schedule_interval => INTERVAL '1 hour',
            if_not_exists => TRUE
        );
        )",

        R"(
        SELECT add_continuous_aggregate_policy('tag_values_1day',
            start_offset => INTERVAL '3 days',
            end_offset => INTERVAL '1 day',
            schedule_interval => INTERVAL '1 day',
            if_not_exists => TRUE
        );
        )"
        R"(
        ALTER TABLE tag_values_raw SET (
            timescaledb.compress,
            timescaledb.compress_segmentby = 'tag_id',
            timescaledb.compress_orderby = 'time DESC'
        );
        )",

        R"(
        SELECT add_compression_policy('tag_values_raw',
            compress_after => INTERVAL '7 days',
            if_not_exists => TRUE
        );
        )",

        R"(
        ALTER MATERIALIZED VIEW tag_values_1min SET (
            timescaledb.compress,
            timescaledb.compress_segmentby = 'tag_id'
        );
        )",

        R"(
        SELECT add_compression_policy('tag_values_1min',
            compress_after => INTERVAL '30 days',
            if_not_exists => TRUE
        );
        )",

        R"(
        ALTER MATERIALIZED VIEW tag_values_5min SET (
            timescaledb.compress,
            timescaledb.compress_segmentby = 'tag_id'
        );
        )",

        R"(
        SELECT add_compression_policy('tag_values_5min',
            compress_after => INTERVAL '90 days',
            if_not_exists => TRUE
        );
        )",

        R"(
        ALTER MATERIALIZED VIEW tag_values_1hour SET (
            timescaledb.compress,
            timescaledb.compress_segmentby = 'tag_id'
        );
        )",

        R"(
        SELECT add_compression_policy('tag_values_1hour',
            compress_after => INTERVAL '1 year',
            if_not_exists => TRUE
        );
        )",
        R"(
        SELECT add_retention_policy('tag_values_raw',
            drop_after => INTERVAL '30 days',
            if_not_exists => TRUE
        );
        )",

        R"(
        SELECT add_retention_policy('tag_values_1min',
            drop_after => INTERVAL '90 days',
            if_not_exists => TRUE
        );
        )",

        R"(
        SELECT add_retention_policy('tag_values_5min',
            drop_after => INTERVAL '1 year',
            if_not_exists => TRUE
        );
        )",

        R"(
        SELECT add_retention_policy('tag_values_1hour',
            drop_after => INTERVAL '5 years',
            if_not_exists => TRUE
        );
        )",
        R"(
        CREATE TABLE IF NOT EXISTS archive_log (
            archive_id BIGSERIAL PRIMARY KEY,
            table_name TEXT NOT NULL,
            file_path TEXT NOT NULL,
            start_time TIMESTAMPTZ,
            end_time TIMESTAMPTZ,
            record_count BIGINT DEFAULT 0,
            file_size_bytes BIGINT DEFAULT 0,
            status TEXT DEFAULT 'completed',
            created_at TIMESTAMPTZ DEFAULT now()
        );
        )",

//        R"(
//            INSERT INTO system_settings (key, value_text, updated_at) VALUES
//            ('api.auth.enabled', 'false', now()),
//            ('api.auth.keys', 'dev-key-123', now())
//            ON CONFLICT (key) DO NOTHING;
//        );
//        )",

        R"(
            CREATE TABLE IF NOT EXISTS dashboards (
                dashboard_id BIGSERIAL PRIMARY KEY,
                name TEXT NOT NULL,
                description TEXT DEFAULT '',
                owner TEXT DEFAULT 'system',
                dashboard_type TEXT DEFAULT 'simple',
                is_public BOOLEAN DEFAULT true,
                created_at TIMESTAMPTZ DEFAULT now(),
                updated_at TIMESTAMPTZ DEFAULT now()
        );
        CREATE INDEX IF NOT EXISTS idx_dashboards_owner ON dashboards(owner);
        CREATE INDEX IF NOT EXISTS idx_dashboards_type ON dashboards(dashboard_type);
        CREATE INDEX IF NOT EXISTS idx_dashboards_is_public ON dashboards(is_public);
        )",

        R"(
        CREATE TABLE IF NOT EXISTS users (
            user_id BIGSERIAL PRIMARY KEY,
            username TEXT NOT NULL UNIQUE,
            password_hash TEXT NOT NULL,
            salt TEXT NOT NULL,
            display_name TEXT DEFAULT '',
            role TEXT DEFAULT 'operator',
            is_active BOOLEAN DEFAULT true,
            last_login_at TIMESTAMPTZ,
            created_at TIMESTAMPTZ DEFAULT now(),
            updated_at TIMESTAMPTZ DEFAULT now()
        );
        )",
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
            driver_id,
            address_config,
            enabled,
            clamp_enabled,
            updated_at
        ) VALUES (
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
            :driver_id,
            :address_config,
            :enabled,
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
            driver_id = EXCLUDED.driver_id,
            address_config = EXCLUDED.address_config,
            enabled = EXCLUDED.enabled,
            clamp_enabled = EXCLUDED.clamp_enabled,
            updated_at = now()
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
    query.bindValue(":storage_deadband", tag.storageDeadband);
    query.bindValue(":alarm_hysteresis", tag.alarmHysteresis);
    query.bindValue(":heartbeat_interval_ms", tag.heartbeatIntervalMs);
    query.bindValue(":software_filter_type", tag.softwareFilter);
    query.bindValue(":software_filter_config", tag.softwareFilterConfig);
    query.bindValue(":sim_profile", tag.simProfile);
    query.bindValue(":driver_id", tag.driverId);
    query.bindValue(":address_config", tag.addressConfig);
    query.bindValue(":enabled", tag.enabled);
    query.bindValue(":clamp_enabled", tag.clampEnabled);

    if (!query.exec()) {
        qWarning() << "Upsert tag failed:" << query.lastError().text();
        return false;
    }

    qInfo() << "Tag upserted:" << tag.tagId << tag.tagName;
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

void DbManager::seedDefaults()
{
    QSqlQuery q(m_db);

    // درایورها فقط اگر نوعشان وجود نداشته باشد
    q.exec(R"(
        INSERT INTO drivers (name, type, connection_config, polling_interval_ms, enabled)
        SELECT 'Simulator', 'simulator', '{}', 1000, true
        WHERE NOT EXISTS (SELECT 1 FROM drivers WHERE type='simulator')
    )");
    q.exec(R"(
        INSERT INTO drivers (name, type, connection_config, polling_interval_ms, enabled)
        SELECT 'Modbus TCP', 'modbus_tcp',
               '{"host":"127.0.0.1","port":502,"default_unit_id":1,"inter_request_delay_ms":50}',
               1000, false
        WHERE NOT EXISTS (SELECT 1 FROM drivers WHERE type='modbus_tcp')
    )");
    q.exec(R"(
        INSERT INTO drivers (name, type, connection_config, polling_interval_ms, enabled)
        SELECT 'OPC UA', 'opc_ua',
               '{"endpoint":"opc.tcp://127.0.0.1:4840","iterate_ms":100}',
               1000, false
        WHERE NOT EXISTS (SELECT 1 FROM drivers WHERE type='opc_ua')
    )");

    // تگ‌های نمونه با lookup نوع درایور
    q.exec(R"(
        INSERT INTO tags
        (tag_id, tag_name, source_type, data_type, eng_units,
         raw_min, raw_max, eng_min, eng_max, scaling_type,
         scaling_slope, scaling_offset, deadband, sim_profile,
         driver_id, address_config, enabled)
        SELECT 9003, 'OPC_Temperature', 'real_driver', 'float', 'C',
               0, 100, 0, 100, 'linear', 1, 0, 0, '',
               d.driver_id, '{"node_id":"ns=2;s=Temperature"}', false
        FROM drivers d WHERE d.type = 'opc_ua'
        ON CONFLICT DO NOTHING
    )");

    qInfo() << "DbManager: default seeds applied (idempotent)";
}

bool DbManager::migrateApiTables()
{
    bool ok = true;

    ok &= execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS dashboards (
            dashboard_id BIGSERIAL PRIMARY KEY,
            name TEXT NOT NULL,
            description TEXT DEFAULT '',
            owner TEXT DEFAULT 'system',
            dashboard_type TEXT DEFAULT 'simple',
            is_public BOOLEAN DEFAULT true,
            created_at TIMESTAMPTZ DEFAULT now(),
            updated_at TIMESTAMPTZ DEFAULT now()
        )
    )", "dashboards");

    ok &= execSql(m_db, R"(
        CREATE INDEX IF NOT EXISTS idx_dashboards_owner
        ON dashboards(owner)
    )", "idx_dashboards_owner");

    ok &= execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS users (
            user_id BIGSERIAL PRIMARY KEY,
            username TEXT NOT NULL UNIQUE,
            password_hash TEXT NOT NULL,
            salt TEXT NOT NULL,
            display_name TEXT DEFAULT '',
            role TEXT DEFAULT 'operator',
            is_active BOOLEAN DEFAULT true,
            last_login_at TIMESTAMPTZ,
            created_at TIMESTAMPTZ DEFAULT now(),
            updated_at TIMESTAMPTZ DEFAULT now()
        )
    )", "users");

    ok &= execSql(m_db, R"(
        INSERT INTO system_settings (key, value_text) VALUES
        ('api.auth.enabled', 'false'),
        ('api.auth.keys', 'dev-key-123')
        ON CONFLICT (key) DO NOTHING
    )", "auth settings");

    return ok;
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
            enabled,
            clamp_enabled
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

QVector<NotificationRule> DbManager::loadNotificationRules()
{
    QVector<NotificationRule> rules;

    QSqlQuery query(m_db);

    query.prepare(R"(
        SELECT
            notification_rule_id,
            name,
            severity_filter,
            alarm_type_filter,
            channel,
            channel_config,
            throttle_ms,
            enabled
        FROM notification_rules
        WHERE enabled = TRUE
        ORDER BY notification_rule_id;
    )");

    if (!query.exec())
    {
        qWarning() << "Load notification rules failed:" << query.lastError().text();
        return rules;
    }

    while (query.next())
    {
        NotificationRule rule;

        rule.notificationRuleId = variantToLongLong(field(query, "notification_rule_id"), 0);
        rule.name = variantToString(field(query, "name"), QString());
        rule.severityFilter = variantToString(field(query, "severity_filter"), QString());
        rule.alarmTypeFilter = variantToString(field(query, "alarm_type_filter"), QString());
        rule.channel = variantToString(field(query, "channel"), QString());
        rule.channelConfig = variantToString(field(query, "channel_config"), QStringLiteral("{}"));
        rule.throttleMs = variantToInt(field(query, "throttle_ms"), 60000);
        rule.enabled = variantToBool(field(query, "enabled"), true);

        rules.push_back(rule);
    }

    return rules;
}

QVector<ComputedTag> DbManager::loadComputedTags()
{
    QVector<ComputedTag> computedTags;

    QSqlQuery query(m_db);

    query.prepare(R"(
        SELECT
            computed_tag_id,
            tag_id,
            expression,
            update_mode,
            update_interval_ms,
            enabled
        FROM computed_tags
        WHERE enabled = TRUE
        ORDER BY computed_tag_id;
    )");

    if (!query.exec())
    {
        qWarning() << "Load computed tags failed:" << query.lastError().text();
        return computedTags;
    }

    while (query.next())
    {
        ComputedTag ct;

        ct.computedTagId = variantToLongLong(field(query, "computed_tag_id"), 0);
        ct.tagId = variantToLongLong(field(query, "tag_id"), 0);
        ct.expression = variantToString(field(query, "expression"), QString());
        ct.updateMode = variantToString(field(query, "update_mode"), QStringLiteral("on_change"));
        ct.updateIntervalMs = variantToInt(field(query, "update_interval_ms"), 1000);
        ct.enabled = variantToBool(field(query, "enabled"), true);

        computedTags.push_back(ct);
    }

    return computedTags;
}


bool DbManager::logNotification(
    qint64 alarmId,
    qint64 notificationRuleId,
    const QString& channel,
    const QString& status,
    const QString& message
)
{
    QSqlQuery query(m_db);

    query.prepare(R"(
        INSERT INTO notification_log (
            alarm_id,
            notification_rule_id,
            channel,
            status,
            message,
            sent_at
        )
        VALUES (
            :alarm_id,
            :notification_rule_id,
            :channel,
            :status,
            :message,
            now()
        );
    )");

    query.bindValue(":alarm_id", alarmId);
    query.bindValue(":notification_rule_id", notificationRuleId);
    query.bindValue(":channel", channel);
    query.bindValue(":status", status);
    query.bindValue(":message", message);

    if (!query.exec())
    {
        qWarning() << "Log notification failed:" << query.lastError().text();
        return false;
    }

    return true;
}

QJsonObject DbManager::getTagCurrentState(int tagId)
{
    QJsonObject result;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT tag_id, last_value, last_quality, last_timestamp, updated_at "
        "FROM tag_current_state WHERE tag_id = :tagId"
    ));
    query.bindValue(":tagId", tagId);

    if (query.exec() && query.next()) {
        result.insert("tag_id", query.value(0).toInt());
        result.insert("value", query.value(1).toDouble());
        result.insert("quality", query.value(2).toInt());
        result.insert("ts", query.value(3).toDateTime().toString(Qt::ISODateWithMs));
        result.insert("updated_at", query.value(4).toDateTime().toString(Qt::ISODateWithMs));
    }

    return result;
}

QVector<QJsonObject> DbManager::getTagsCurrentState(const QVector<int>& tagIds)
{
    QVector<QJsonObject> results;

    if (tagIds.isEmpty()) {
        return results;
    }

    QStringList placeholders;
    for (int i = 0; i < tagIds.size(); ++i) {
        placeholders << QString(":id%1").arg(i);
    }

    QString sql = QStringLiteral(
        "SELECT tag_id, last_value, last_quality, last_timestamp, updated_at "
        "FROM tag_current_state WHERE tag_id IN (%1)"
    ).arg(placeholders.join(","));

    QSqlQuery query(m_db);
    query.prepare(sql);

    for (int i = 0; i < tagIds.size(); ++i) {
        query.bindValue(QString(":id%1").arg(i), tagIds[i]);
    }

    if (query.exec()) {
        while (query.next()) {
            QJsonObject obj;
            obj.insert("tag_id", query.value(0).toInt());
            obj.insert("value", query.value(1).toDouble());
            obj.insert("quality", query.value(2).toInt());
            obj.insert("ts", query.value(3).toDateTime().toString(Qt::ISODateWithMs));
            obj.insert("updated_at", query.value(4).toDateTime().toString(Qt::ISODateWithMs));
            results.append(obj);
        }
    }

    return results;
}

QVector<QJsonObject> DbManager::loadAlarms(int limit, int offset)
{
    QVector<QJsonObject> alarms;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT alarm_id, tag_id, alarm_type, severity, state, value, threshold, "
        "message, active_time, clear_time, ack_time, ack_user "
        "FROM alarms "
        "ORDER BY alarm_id DESC "
        "LIMIT :limit OFFSET :offset"
    ));
    query.bindValue(":limit", limit);
    query.bindValue(":offset", offset);

    if (!query.exec()) {
        qWarning() << "Load alarms failed:" << query.lastError().text();
        return alarms;
    }

    while (query.next()) {
        QJsonObject alarm;
        alarm.insert("alarm_id", query.value(0).toLongLong());
        alarm.insert("tag_id", query.value(1).toLongLong());
        alarm.insert("alarm_type", query.value(2).toString());
        alarm.insert("severity", query.value(3).toString());
        alarm.insert("state", query.value(4).toString());
        alarm.insert("value", query.value(5).toDouble());
        alarm.insert("threshold", query.value(6).toDouble());
        alarm.insert("message", query.value(7).toString());

        if (!query.value(8).isNull()) {
            alarm.insert("active_time", query.value(8).toDateTime().toString(Qt::ISODateWithMs));
        }
        if (!query.value(9).isNull()) {
            alarm.insert("clear_time", query.value(9).toDateTime().toString(Qt::ISODateWithMs));
        }
        if (!query.value(10).isNull()) {
            alarm.insert("ack_time", query.value(10).toDateTime().toString(Qt::ISODateWithMs));
        }
        if (!query.value(11).isNull()) {
            alarm.insert("ack_user", query.value(11).toString());
        }

        alarms.append(alarm);
    }

    return alarms;
}



QSqlDatabase DbManager::database() const
{
    return m_db;
}

bool DbManager::deleteTag(qint64 tagId)
{
    if (!m_db.transaction()) {
        qWarning() << "Cannot start transaction for deleteTag:" << m_db.lastError().text();
        return false;
    }

    QSqlQuery query(m_db);

    // اول rule های مرتبط را حذف کن
    query.prepare("DELETE FROM threshold_rules WHERE tag_id = :tag_id;");
    query.bindValue(":tag_id", tagId);
    if (!query.exec()) {
        qWarning() << "Delete threshold_rules failed:" << query.lastError().text();
        m_db.rollback();
        return false;
    }

    query.prepare("DELETE FROM range_violation_rules WHERE tag_id = :tag_id;");
    query.bindValue(":tag_id", tagId);
    query.exec();

    query.prepare("DELETE FROM rate_of_change_rules WHERE tag_id = :tag_id;");
    query.bindValue(":tag_id", tagId);
    query.exec();

    query.prepare("DELETE FROM stuck_value_rules WHERE tag_id = :tag_id;");
    query.bindValue(":tag_id", tagId);
    query.exec();

    query.prepare("DELETE FROM boolean_rules WHERE tag_id = :tag_id;");
    query.bindValue(":tag_id", tagId);
    query.exec();

    // بعد current state را حذف کن
    query.prepare("DELETE FROM tag_current_state WHERE tag_id = :tag_id;");
    query.bindValue(":tag_id", tagId);
    query.exec();

    // در نهایت خود tag را حذف کن
    query.prepare("DELETE FROM tags WHERE tag_id = :tag_id;");
    query.bindValue(":tag_id", tagId);
    if (!query.exec()) {
        qWarning() << "Delete tag failed:" << query.lastError().text();
        m_db.rollback();
        return false;
    }

    const int affectedRows = query.numRowsAffected();

    if (!m_db.commit()) {
        qWarning() << "Cannot commit deleteTag:" << m_db.lastError().text();
        m_db.rollback();
        return false;
    }

    qInfo() << "Tag deleted:" << tagId;
    return affectedRows > 0;
}

QVector<QJsonObject> DbManager::queryTagHistory(
    qint64 tagId,
    const QDateTime& fromTime,
    const QDateTime& toTime,
    const QString& interval,
    int limit)
{
    QVector<QJsonObject> results;

    QSqlQuery query(m_db);

    if (interval.isEmpty()) {
        // Raw data query
        query.prepare(QStringLiteral(
            "SELECT time, raw_value, eng_value, quality, source "
            "FROM tag_values_raw "
            "WHERE tag_id = :tag_id "
            "AND time >= :from_time AND time < :to_time "
            "ORDER BY time DESC "
            "LIMIT :limit"
        ));
        query.bindValue(":tag_id", tagId);
        query.bindValue(":from_time", fromTime);
        query.bindValue(":to_time", toTime);
        query.bindValue(":limit", limit);

        if (!query.exec()) {
            qWarning() << "Query tag history failed:" << query.lastError().text();
            return results;
        }

        while (query.next()) {
            QJsonObject obj;
            obj.insert("time", query.value(0).toDateTime().toString(Qt::ISODateWithMs));
            obj.insert("raw_value", query.value(1).toDouble());
            obj.insert("eng_value", query.value(2).toDouble());
            obj.insert("quality", query.value(3).toInt());
            obj.insert("source", query.value(4).toString());
            results.append(obj);
        }
    } else {
        // Aggregated query با time_bucket
        QString intervalStr = interval;

        // اعتبارسنجی ساده interval برای جلوگیری از SQL injection
        if (intervalStr != "1 second" && intervalStr != "1 minute" &&
            intervalStr != "5 minutes" && intervalStr != "1 hour" &&
            intervalStr != "1 day") {
            intervalStr = "1 minute";
        }

        const QString sql = QStringLiteral(
            "SELECT "
            "  time_bucket(INTERVAL '%1', time) AS bucket, "
            "  avg(eng_value) AS avg_value, "
            "  min(eng_value) AS min_value, "
            "  max(eng_value) AS max_value, "
            "  first(eng_value, time) AS first_value, "
            "  last(eng_value, time) AS last_value, "
            "  count(*) AS sample_count "
            "FROM tag_values_raw "
            "WHERE tag_id = :tag_id "
            "AND time >= :from_time AND time < :to_time "
            "GROUP BY bucket "
            "ORDER BY bucket ASC "
            "LIMIT :limit"
        ).arg(intervalStr);

        query.prepare(sql);
        query.bindValue(":tag_id", tagId);
        query.bindValue(":from_time", fromTime);
        query.bindValue(":to_time", toTime);
        query.bindValue(":limit", limit);

        if (!query.exec()) {
            qWarning() << "Query tag history aggregate failed:" << query.lastError().text();
            return results;
        }

        while (query.next()) {
            QJsonObject obj;
            obj.insert("bucket", query.value(0).toDateTime().toString(Qt::ISODateWithMs));
            obj.insert("avg", query.value(1).toDouble());
            obj.insert("min", query.value(2).toDouble());
            obj.insert("max", query.value(3).toDouble());
            obj.insert("first", query.value(4).toDouble());
            obj.insert("last", query.value(5).toDouble());
            obj.insert("count", query.value(6).toInt());
            results.append(obj);
        }
    }

    return results;
}

QVector<DashboardDefinition> DbManager::loadDashboards()
{
    QVector<DashboardDefinition> dashboards;

    QSqlQuery query(m_db);
    query.prepare(R"(
        SELECT dashboard_id, name, description, owner, dashboard_type,
               is_public, created_at, updated_at
        FROM dashboards
        ORDER BY dashboard_id
    )");

    if (!query.exec()) {
        qWarning() << "Load dashboards failed:" << query.lastError().text();
        return dashboards;
    }

    while (query.next()) {
        DashboardDefinition d;
        d.dashboardId = query.value(0).toLongLong();
        d.name = query.value(1).toString();
        d.description = query.value(2).toString();
        d.owner = query.value(3).toString();
        d.dashboardType = query.value(4).toString();
        d.isPublic = query.value(5).toBool();
        d.createdAt = query.value(6).toDateTime();
        d.updatedAt = query.value(7).toDateTime();
        dashboards.append(d);
    }

    return dashboards;
}

DashboardDefinition DbManager::loadDashboard(qint64 dashboardId)
{
    DashboardDefinition d;

    QSqlQuery query(m_db);
    query.prepare(R"(
        SELECT dashboard_id, name, description, owner, dashboard_type,
               is_public, created_at, updated_at
        FROM dashboards
        WHERE dashboard_id = :id
    )");
    query.bindValue(":id", dashboardId);

    if (!query.exec()) {
        qWarning() << "Load dashboard failed:" << query.lastError().text();
        return d;
    }

    if (query.next()) {
        d.dashboardId = query.value(0).toLongLong();
        d.name = query.value(1).toString();
        d.description = query.value(2).toString();
        d.owner = query.value(3).toString();
        d.dashboardType = query.value(4).toString();
        d.isPublic = query.value(5).toBool();
        d.createdAt = query.value(6).toDateTime();
        d.updatedAt = query.value(7).toDateTime();
    }

    return d;
}

qint64 DbManager::insertDashboard(const DashboardDefinition& dashboard)
{
    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT INTO dashboards (name, description, owner, dashboard_type, is_public)
        VALUES (:name, :description, :owner, :dashboard_type, :is_public)
        RETURNING dashboard_id
    )");

    query.bindValue(":name", dashboard.name);
    query.bindValue(":description", dashboard.description);
    query.bindValue(":owner", dashboard.owner.isEmpty() ? "system" : dashboard.owner);
    query.bindValue(":dashboard_type", dashboard.dashboardType.isEmpty() ? "simple" : dashboard.dashboardType);
    query.bindValue(":is_public", dashboard.isPublic);

    if (!query.exec()) {
        qWarning() << "Insert dashboard failed:" << query.lastError().text();
        return -1;
    }

    if (query.next()) {
        const qint64 newId = query.value(0).toLongLong();
        qInfo() << "Dashboard created:" << newId << dashboard.name;
        return newId;
    }

    return -1;
}

bool DbManager::updateDashboard(const DashboardDefinition& dashboard)
{
    QSqlQuery query(m_db);
    query.prepare(R"(
        UPDATE dashboards
        SET name = :name,
            description = :description,
            owner = :owner,
            dashboard_type = :dashboard_type,
            is_public = :is_public,
            updated_at = now()
        WHERE dashboard_id = :id
    )");

    query.bindValue(":name", dashboard.name);
    query.bindValue(":description", dashboard.description);
    query.bindValue(":owner", dashboard.owner);
    query.bindValue(":dashboard_type", dashboard.dashboardType);
    query.bindValue(":is_public", dashboard.isPublic);
    query.bindValue(":id", dashboard.dashboardId);

    if (!query.exec()) {
        qWarning() << "Update dashboard failed:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

bool DbManager::deleteDashboard(qint64 dashboardId)
{
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM dashboards WHERE dashboard_id = :id");
    query.bindValue(":id", dashboardId);

    if (!query.exec()) {
        qWarning() << "Delete dashboard failed:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

bool DbManager::touchDashboard(qint64 dashboardId)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE dashboards SET updated_at = now() WHERE dashboard_id = :id");
    query.bindValue(":id", dashboardId);
    return query.exec();
}

int DbManager::userCount()
{
    QSqlQuery query(m_db);
    if (!query.exec("SELECT count(*) FROM users;")) return -1;
    if (query.next()) return query.value(0).toInt();
    return -1;
}

static UserDefinition readUserRow(QSqlQuery& query, bool withSecret)
{
    UserDefinition u;
    u.userId = query.value(0).toLongLong();
    u.username = query.value(1).toString();
    if (withSecret) {
        u.passwordHash = query.value(2).toString();
        u.salt = query.value(3).toString();
        u.displayName = query.value(4).toString();
        u.role = query.value(5).toString();
        u.isActive = query.value(6).toBool();
    } else {
        u.displayName = query.value(2).toString();
        u.role = query.value(3).toString();
        u.isActive = query.value(4).toBool();
    }
    return u;
}

UserDefinition DbManager::loadUserByUsername(const QString& username)
{
    UserDefinition u;
    QSqlQuery query(m_db);
    query.prepare("SELECT user_id, username, password_hash, salt, display_name, role, is_active "
                  "FROM users WHERE username = :username");
    query.bindValue(":username", username);
    if (query.exec() && query.next()) u = readUserRow(query, true);
    return u;
}

UserDefinition DbManager::loadUserById(qint64 userId)
{
    UserDefinition u;
    QSqlQuery query(m_db);
    query.prepare("SELECT user_id, username, password_hash, salt, display_name, role, is_active "
                  "FROM users WHERE user_id = :id");
    query.bindValue(":id", userId);
    if (query.exec() && query.next()) u = readUserRow(query, true);
    return u;
}

QVector<UserDefinition> DbManager::loadUsers()
{
    QVector<UserDefinition> users;
    QSqlQuery query(m_db);
    query.prepare("SELECT user_id, username, display_name, role, is_active FROM users ORDER BY user_id");
    if (!query.exec()) return users;
    while (query.next()) users.append(readUserRow(query, false));
    return users;
}

qint64 DbManager::insertUserRaw(const QString& username, const QString& passwordHash,
                                const QString& salt, const QString& displayName, const QString& role)
{
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO users (username, password_hash, salt, display_name, role) "
                  "VALUES (:username, :hash, :salt, :display, :role) RETURNING user_id");
    query.bindValue(":username", username);
    query.bindValue(":hash", passwordHash);
    query.bindValue(":salt", salt);
    query.bindValue(":display", displayName);
    query.bindValue(":role", role);
    if (!query.exec()) {
        qWarning() << "Insert user failed:" << query.lastError().text();
        return -1;
    }
    if (query.next()) return query.value(0).toLongLong();
    return -1;
}

bool DbManager::updateUser(const UserDefinition& user)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE users SET display_name = :display, role = :role, is_active = :active, "
                  "updated_at = now() WHERE user_id = :id");
    query.bindValue(":display", user.displayName);
    query.bindValue(":role", user.role);
    query.bindValue(":active", user.isActive);
    query.bindValue(":id", user.userId);
    return query.exec() && query.numRowsAffected() > 0;
}

bool DbManager::deleteUser(qint64 userId)
{
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM users WHERE user_id = :id");
    query.bindValue(":id", userId);
    return query.exec() && query.numRowsAffected() > 0;
}

bool DbManager::updateUserPassword(qint64 userId, const QString& passwordHash, const QString& salt)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE users SET password_hash = :hash, salt = :salt, updated_at = now() "
                  "WHERE user_id = :id");
    query.bindValue(":hash", passwordHash);
    query.bindValue(":salt", salt);
    query.bindValue(":id", userId);
    return query.exec() && query.numRowsAffected() > 0;
}

bool DbManager::touchLastLogin(qint64 userId)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE users SET last_login_at = now() WHERE user_id = :id");
    query.bindValue(":id", userId);
    return query.exec();
}

DriverDefinition DbManager::loadDriver(qint64 driverId)
{
    DriverDefinition d;
    QSqlQuery query(m_db);
    query.prepare("SELECT driver_id, name, type, connection_config, polling_interval_ms, enabled "
                  "FROM drivers WHERE driver_id = :id");
    query.bindValue(":id", driverId);
    if (query.exec() && query.next()) {
        d.driverId = query.value(0).toLongLong();
        d.name = query.value(1).toString();
        d.type = query.value(2).toString();
        d.connectionConfig = query.value(3).toString();
        d.pollingIntervalMs = query.value(4).toInt();
        d.enabled = query.value(5).toBool();
    }
    return d;
}

qint64 DbManager::insertDriver(const DriverDefinition& driver)
{
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO drivers (name, type, connection_config, polling_interval_ms, enabled) "
                  "VALUES (:name, :type, :config, :polling, :enabled) RETURNING driver_id");
    query.bindValue(":name", driver.name);
    query.bindValue(":type", driver.type);
    query.bindValue(":config", driver.connectionConfig.isEmpty() ? "{}" : driver.connectionConfig);
    query.bindValue(":polling", driver.pollingIntervalMs);
    query.bindValue(":enabled", driver.enabled);
    if (!query.exec()) {
        qWarning() << "Insert driver failed:" << query.lastError().text();
        return -1;
    }
    if (query.next()) return query.value(0).toLongLong();
    return -1;
}

bool DbManager::updateDriver(const DriverDefinition& driver)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE drivers SET name = :name, type = :type, connection_config = :config, "
                  "polling_interval_ms = :polling, enabled = :enabled WHERE driver_id = :id");
    query.bindValue(":name", driver.name);
    query.bindValue(":type", driver.type);
    query.bindValue(":config", driver.connectionConfig);
    query.bindValue(":polling", driver.pollingIntervalMs);
    query.bindValue(":enabled", driver.enabled);
    query.bindValue(":id", driver.driverId);
    return query.exec() && query.numRowsAffected() > 0;
}

bool DbManager::deleteDriver(qint64 driverId)
{
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM drivers WHERE driver_id = :id");
    query.bindValue(":id", driverId);
    return query.exec() && query.numRowsAffected() > 0;
}

int DbManager::tagCountForDriver(qint64 driverId)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT count(*) FROM tags WHERE driver_id = :id");
    query.bindValue(":id", driverId);
    if (query.exec() && query.next()) return query.value(0).toInt();
    return -1;
}






