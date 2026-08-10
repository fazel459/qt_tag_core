#include "ComputedTagEngine.h"

#include <QDebug>
#include <QRegularExpression>

ComputedTagEngine::ComputedTagEngine(
    TagBus& bus,
    DbManager& db,
    const AppConfig& config,
    QObject* parent
)
    : QObject(parent)
    , m_bus(bus)
    , m_db(db)
    , m_config(config)
{
    for (const TagDefinition& tag : config.tags)
    {
        m_tagNames[tag.tagId] = tag.tagName;
    }

    for (const ComputedTag& ct : config.computedTags)
    {
        m_computedTagsById[ct.computedTagId] = ct;

        const QSet<QString> tagNamesInExpr = extractTagNames(ct.expression);

        for (const QString& tagName : tagNamesInExpr)
        {
            for (auto it = m_tagNames.begin(); it != m_tagNames.end(); ++it)
            {
                if (it.value() == tagName)
                {
                    m_dependencyMap[it.key()].insert(ct.tagId);
                    break;
                }
            }
        }
    }

    m_bus.subscribe("tags/#", [this](const BusMessage& message)
    {
        if (!message.topic.endsWith("/update"))
        {
            return;
        }

        onTagUpdate(message.value);
    });

    m_periodicTimer.setParent(this);
    m_periodicTimer.setInterval(1000);

    QObject::connect(&m_periodicTimer, &QTimer::timeout, [this]()
    {
        evaluateAllComputedTags();
    });

    bool hasPeriodic = false;

    for (const ComputedTag& ct : config.computedTags)
    {
        if (ct.updateMode == "periodic")
        {
            hasPeriodic = true;
            break;
        }
    }

    if (hasPeriodic)
    {
        m_periodicTimer.start();
    }

    qInfo() << "ComputedTagEngine started:"
            << "computedTags=" << config.computedTags.size();
}

void ComputedTagEngine::onTagUpdate(const TagValue& value)
{
    m_latestValues[value.tagId] = value.engineeringValue;

    const QSet<qint64>& dependents = m_dependencyMap.value(value.tagId);

    for (qint64 computedTagId : dependents)
    {
        for (const ComputedTag& ct : m_config.computedTags)
        {
            if (ct.tagId == computedTagId && ct.updateMode == "on_change")
            {
                evaluateComputedTag(ct);
            }
        }
    }

//    qInfo() << "ComputedTagEngine: tag update:"
//            << "tagId=" << value.tagId
//            << "value=" << value.engineeringValue;
}

void ComputedTagEngine::evaluateAllComputedTags()
{
    for (const ComputedTag& ct : m_config.computedTags)
    {
        if (ct.updateMode == "periodic")
        {
            evaluateComputedTag(ct);
        }
    }
}

void ComputedTagEngine::evaluateComputedTag(const ComputedTag& ct)
{
    QHash<QString, double> variables;

    const QSet<QString> tagNamesInExpr = extractTagNames(ct.expression);

    for (const QString& tagName : tagNamesInExpr)
    {
        for (auto it = m_tagNames.begin(); it != m_tagNames.end(); ++it)
        {
            if (it.value() == tagName)
            {
                const qint64 tagId = it.key();

                if (m_latestValues.contains(tagId))
                {
                    variables[tagName] = m_latestValues[tagId];
                }
                else
                {
                    variables[tagName] = 0.0;
                }

                break;
            }
        }
    }

    const double result = evaluateExpression(ct.expression, variables);

    TagValue outputValue;

    outputValue.tagId = ct.tagId;
    outputValue.tagName = m_tagNames.value(ct.tagId);
    outputValue.timestamp = QDateTime::currentDateTimeUtc();
    outputValue.rawValue = result;
    outputValue.engineeringValue = result;
    outputValue.quality = Quality::Good;
    outputValue.source = SourceKind::Calculated;

    const QString topic = QStringLiteral("tags/%1/update").arg(ct.tagId);

    qInfo() << "ComputedTagEngine: evaluating:"
            << "tagId=" << ct.tagId
            << "expression=" << ct.expression
            << "result=" << result;
    m_bus.publish(topic, outputValue);
}

QSet<QString> ComputedTagEngine::extractTagNames(const QString& expression) const
{
    QSet<QString> tagNames;

    QRegularExpression re(R"([A-Za-z_][A-Za-z0-9_.]*)");

    QRegularExpressionMatchIterator it = re.globalMatch(expression);

    while (it.hasNext())
    {
        QRegularExpressionMatch match = it.next();
        const QString candidate = match.captured(0);

        bool isTag = false;

        for (auto tagIt = m_tagNames.begin(); tagIt != m_tagNames.end(); ++tagIt)
        {
            if (tagIt.value() == candidate)
            {
                isTag = true;
                break;
            }
        }

        if (isTag)
        {
            tagNames.insert(candidate);
        }
    }

    return tagNames;
}

double ComputedTagEngine::evaluateExpression(
    const QString& expression,
    const QHash<QString, double>& variables
)
{
    QJSValue globalObj = m_jsEngine.globalObject();

    for (auto it = variables.begin(); it != variables.end(); ++it)
    {
        QString safeName = it.key();
        safeName.replace('.', '_');
        safeName.replace('-', '_');

        globalObj.setProperty(safeName, it.value());
    }

    QString safeExpression = expression;

    for (auto it = variables.begin(); it != variables.end(); ++it)
    {
        QString safeName = it.key();
        safeName.replace('.', '_');
        safeName.replace('-', '_');

        safeExpression.replace(it.key(), safeName);
    }

    QJSValue result = m_jsEngine.evaluate(safeExpression);

    if (result.isError())
    {
        qWarning() << "ComputedTagEngine: expression error:"
                   << result.toString()
                   << "expression:" << expression;
        return 0.0;
    }

    if (result.isNumber())
    {
        return result.toNumber();
    }

    qWarning() << "ComputedTagEngine: expression did not return a number:"
               << expression;

    return 0.0;
}

