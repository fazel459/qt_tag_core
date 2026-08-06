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

    double deadband = 0.0;

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

    bool hasLow = false;
    bool hasHigh = false;

    double low = 0.0;
    double high = 0.0;
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

    QVector<TagDefinition> tags;
    QVector<ThresholdRule> rules;
};