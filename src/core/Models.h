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

struct DashboardDefinition {
    qint64 dashboardId = 0;
    QString name;
    QString description;
    QString owner;
    QString dashboardType = "simple";  // simple, qml, html, template
    QString config;  // JSON string
    bool isPublic = true;
    QDateTime createdAt;
    QDateTime updatedAt;
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

    qint64 driverId = 0;
    QString addressConfig = "{}";

    bool enabled = true;
    bool clampEnabled = true;
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
    qint64 ruleId = 0;
    qint64 tagId = 0;

    bool hasLowLow = false;
    bool hasLow = false;
    bool hasHigh = false;
    bool hasHighHigh = false;

    double lowLow = 0.0;
    double low = 0.0;
    double high = 0.0;
    double highHigh = 0.0;

    double lowLowHysteresis = -1.0;
    double lowHysteresis = -1.0;
    double highHysteresis = -1.0;
    double highHighHysteresis = -1.0;

    int onDelayMs = -1;
    int offDelayMs = -1;
};

struct DriverDefinition
{
    qint64 driverId = 0;
    QString name;
    QString type;
    QString connectionConfig = "{}";
    int pollingIntervalMs = 1000;
    bool enabled = true;
};


enum class AlarmSeverity
{
    Info,
    Low,
    Medium,
    High,
    Critical
};

inline QString alarmSeverityToString(AlarmSeverity severity)
{
    switch (severity)
    {
        case AlarmSeverity::Info:     return QStringLiteral("info");
        case AlarmSeverity::Low:      return QStringLiteral("low");
        case AlarmSeverity::Medium:   return QStringLiteral("medium");
        case AlarmSeverity::High:     return QStringLiteral("high");
        case AlarmSeverity::Critical: return QStringLiteral("critical");
    }
    return QStringLiteral("info");
}

inline AlarmSeverity stringToAlarmSeverity(const QString& str)
{
    const QString s = str.trimmed().toLower();

    if (s == "critical") return AlarmSeverity::Critical;
    if (s == "high")     return AlarmSeverity::High;
    if (s == "medium")   return AlarmSeverity::Medium;
    if (s == "low")      return AlarmSeverity::Low;

    return AlarmSeverity::Info;
}

struct RangeViolationRule
{
    qint64 ruleId = 0;
    qint64 tagId = 0;

    double minValue = 0.0;
    double maxValue = 100.0;

    QString severity = "high";
};

struct RateOfChangeRule
{
    qint64 ruleId = 0;
    qint64 tagId = 0;

    double maxRatePerSecond = 10.0;
    int windowMs = 5000;

    QString severity = "high";
};

struct StuckValueRule
{
    qint64 ruleId = 0;
    qint64 tagId = 0;

    int stuckDurationMs = 60000;
    double epsilon = 0.01;

    QString severity = "medium";
};

struct BooleanRule
{
    qint64 ruleId = 0;
    qint64 tagId = 0;

    bool alarmOnTrue = false;
    bool alarmOnFalse = false;

    int durationMs = 1000;

    QString severity = "medium";
};

struct NotificationRule
{
    qint64 notificationRuleId = 0;
    QString name;
    QString severityFilter;
    QString alarmTypeFilter;
    QString channel;
    QString channelConfig = "{}";
    int throttleMs = 60000;
    bool enabled = true;
};

struct AlarmNotification
{
    qint64 alarmId = 0;
    qint64 tagId = 0;
    QString tagName;
    QString alarmType;
    QString severity;
    QString state;
    double value = 0.0;
    double threshold = 0.0;
    QString message;
    QDateTime timestamp;
};

struct ComputedTag
{
    qint64 computedTagId = 0;
    qint64 tagId = 0;
    QString expression;
    QString updateMode = "on_change";
    int updateIntervalMs = 1000;
    bool enabled = true;
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
    QVector<DriverDefinition> drivers;
    int engineeringDecimals = 4;

    QVector<RangeViolationRule> rangeViolationRules;
    QVector<RateOfChangeRule> rateOfChangeRules;
    QVector<StuckValueRule> stuckValueRules;
    QVector<BooleanRule> booleanRules;
    QVector<NotificationRule> notificationRules;
    QVector<ComputedTag> computedTags;
};

