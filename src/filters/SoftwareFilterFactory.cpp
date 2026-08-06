#include "SoftwareFilterFactory.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "SoftwareFilters.h"

ISoftwareFilter* SoftwareFilterFactory::create(const TagDefinition& tag)
{
    const QString type = tag.softwareFilter.trimmed().toLower();

    if (type.isEmpty() || type == "none")
    {
        return new NoneFilter();
    }

    QJsonObject config;

    if (!tag.softwareFilterConfig.isEmpty())
    {
        QJsonParseError parseError {};

        const QJsonDocument doc =
            QJsonDocument::fromJson(tag.softwareFilterConfig.toUtf8(), &parseError);

        if (parseError.error == QJsonParseError::NoError && doc.isObject())
        {
            config = doc.object();
        }
        else
        {
            qWarning() << "Invalid software_filter_config for tag"
                       << tag.tagName
                       << "- using defaults. Error:"
                       << parseError.errorString();
        }
    }

    if (type == "moving_average")
    {
        return new MovingAverageFilter(config.value("window").toInt(5));
    }

    if (type == "exponential_average")
    {
        return new ExponentialAverageFilter(config.value("alpha").toDouble(0.2));
    }

    if (type == "median")
    {
        return new MedianFilter(config.value("window").toInt(5));
    }

    if (type == "debounce")
    {
        return new DebounceFilter(
            config.value("debounce_ms").toInt(1000),
            config.value("epsilon").toDouble(0.01)
        );
    }

    if (type == "outlier_rejection")
    {
        return new OutlierRejectionFilter(
            config.value("max_delta").toDouble(5.0),
            config.value("accept_after_n_outliers").toInt(3)
        );
    }

    if (type == "rate_limiter")
    {
        return new RateLimiterFilter(
            config.value("max_rate_per_second").toDouble(1.0)
        );
    }

    qWarning() << "Unknown software filter type:"
               << type
               << "for tag:"
               << tag.tagName
               << "- using none";

    return new NoneFilter();
}
