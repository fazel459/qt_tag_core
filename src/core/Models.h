#pragma once

#include <QString>
#include <QDateTime>
#include <QVector>
#include <QtGlobal>

enum class Quality : int
{
    Good = 0,
    Uncertain = 1,
    Bad = 2
};

enum class SourceKind
{
    RealDriver,
    Simulator,
    Calculated,
    Manual,
    Replay
};

struct TagDefinition
{
    qint64 tagId = 0;
    QString tagName;

    QString sourceType = "simulator";
    QString dataType = "float";
    QString engUnits;

    double rawMin = 0.0;
    double rawMax = 100.0;

    double engMin = 0.0;
    double engMax = 100.0;

    QString scalingType = "linear";
    double slope = 1.0;
    double offset = 0.0;

    // Legacy deadband; اگر storage_deadband ست نشده باشد، از این استفاده می‌شود.
    double deadband = 0.0;

    // Deadband مخصوص ذخیره تاریخی
    // اگر منفی باشد، از deadband قبلی استفاده می‌شود.
    double storageDeadband = -1.0;

    // Hysteresis مخصوص آلارم برای همین تگ
    // اگر منفی باشد، از مقدار پیش‌فرض/global/rule استفاده می‌شود.
    double alarmHysteresis = -1.0;

    // Heartbeat برای ذخیره تاریخی
    // اگر منفی باشد، از مقدار پیش‌فرض استفاده می‌شود.
    int heartbeatIntervalMs = -1;

    // برای فاز بعدی: فیلترهای نرم‌افزاری
    // none, moving_average, exponential_average, median, debounce, outlier_rejection
    QString softwareFilter = "none";
    QString softwareFilterConfig = "{}";
    QString simProfile = "sine";

    bool enabled = true;

};

struct TagValue
{
    qint64 tagId = 0;
    QString tagName;

    QDateTime timestamp;

    double rawValue = 0.0;
    double engineeringValue = 0.0;

    Quality quality = Quality::Good;
    SourceKind source = SourceKind::Simulator;

    quint64 sequence = 0;
};

struct ThresholdRule
{
    qint64 tagId = 0;
    qint64 ruleId = 0;

    bool hasLow = false;
    bool hasHigh = false;

    double low = 0.0;
    double high = 0.0;

    // اگر منفی باشد، از مقدار پیش‌فرض استفاده می‌شود.
    double highHysteresis = -1.0;
    double lowHysteresis = -1.0;

    int onDelayMs = -1;
    int offDelayMs = -1;



};

struct AppConfig
{
    QString dbDriver = "auto";
    QString odbcDriver = "PostgreSQL Unicode";

    QString dbHost = "localhost";
    int dbPort = 5432;
    QString dbName = "tagsdb";
    QString dbUser = "postgres";
    QString dbPassword = "postgres";

    int batchFlushIntervalMs = 500;
    int batchMaxSize = 1000;

    double globalMinDeadband = 0.0;

    double defaultAlarmHysteresis = 0.0;
    int defaultAlarmOnDelayMs = 0;
    int defaultAlarmOffDelayMs = 0;

    int badQualityDelayMs = 0;

    int defaultHeartbeatIntervalMs = 30000;

    int currentStateFlushIntervalMs = 500;

    QVector<TagDefinition> tags;
    QVector<ThresholdRule> rules;
    int engineeringDecimals = 4;
};
