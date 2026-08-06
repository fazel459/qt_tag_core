#include "ConfigLoader.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariant>
#include <QDebug>

std::optional<AppConfig> ConfigLoader::load(const QString& path)
{
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "Cannot open config file:" << path;
        return std::nullopt;
    }

    const QByteArray data = file.readAll();

    QJsonParseError parseError {};
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError)
    {
        qWarning() << "JSON parse error:" << parseError.errorString();
        return std::nullopt;
    }

    if (!doc.isObject())
    {
        qWarning() << "Config root must be an object";
        return std::nullopt;
    }

    AppConfig cfg;

    const QJsonObject root = doc.object();

    const QJsonObject db = root.value("database").toObject();

    cfg.dbDriver = db.value("driver").toString("auto");
    cfg.odbcDriver = db.value("odbc_driver").toString("PostgreSQL Unicode");

    cfg.dbHost = db.value("host").toString("localhost");
    cfg.dbPort = db.value("port").toInt(5432);
    cfg.dbName = db.value("database").toString("tagsdb");
    cfg.dbUser = db.value("username").toString("postgres");
    cfg.dbPassword = db.value("password").toString("postgres");

    const QJsonArray tags = root.value("tags").toArray();

    for (const QJsonValue& value : tags)
    {
        const QJsonObject obj = value.toObject();

        TagDefinition tag;

        tag.tagId = obj.value("tag_id").toVariant().toLongLong();
        tag.tagName = obj.value("tag_name").toString();
        tag.sourceType = obj.value("source_type").toString("simulator");
        tag.dataType = obj.value("data_type").toString("float");
        tag.engUnits = obj.value("eng_units").toString();

        tag.rawMin = obj.value("raw_min").toDouble(0.0);
        tag.rawMax = obj.value("raw_max").toDouble(100.0);

        tag.engMin = obj.value("eng_min").toDouble(0.0);
        tag.engMax = obj.value("eng_max").toDouble(100.0);

        tag.scalingType = obj.value("scaling_type").toString("linear");
        tag.slope = obj.value("slope").toDouble(1.0);
        tag.offset = obj.value("offset").toDouble(0.0);

        tag.deadband = obj.value("deadband").toDouble(0.0);

        tag.simProfile = obj.value("sim_profile").toString("sine");

        tag.enabled = obj.value("enabled").toBool(true);

        if (tag.tagId > 0 && !tag.tagName.isEmpty())
        {
            cfg.tags.push_back(tag);
        }
        else
        {
            qWarning() << "Invalid tag definition skipped:" << tag.tagName;
        }
    }

    const QJsonArray rules = root.value("rules").toArray();

    for (const QJsonValue& value : rules)
    {
        const QJsonObject obj = value.toObject();

        ThresholdRule rule;

        rule.tagId = obj.value("tag_id").toVariant().toLongLong();

        if (obj.contains("low"))
        {
            rule.hasLow = true;
            rule.low = obj.value("low").toDouble();
        }

        if (obj.contains("high"))
        {
            rule.hasHigh = true;
            rule.high = obj.value("high").toDouble();
        }

        if (rule.tagId > 0 && (rule.hasLow || rule.hasHigh))
        {
            cfg.rules.push_back(rule);
        }
        else
        {
            qWarning() << "Invalid threshold rule skipped for tag:" << rule.tagId;
        }
    }

    return cfg;
}