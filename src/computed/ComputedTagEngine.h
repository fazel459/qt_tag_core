#ifndef COMPUTEDTAGENGINE_H
#define COMPUTEDTAGENGINE_H
#pragma once

#include <QHash>
#include <QJSEngine>
#include <QObject>
#include <QSet>
#include <QTimer>

#include "../core/Models.h"
#include "../storage/DbManager.h"
#include "../tagbus/TagBus.h"

class ComputedTagEngine : public QObject
{
public:
    ComputedTagEngine(
        TagBus& bus,
        DbManager& db,
        const AppConfig& config,
        QObject* parent = nullptr
    );

private:
    void onTagUpdate(const TagValue& value);

    void evaluateComputedTag(const ComputedTag& ct);

    void evaluateAllComputedTags();

    QSet<QString> extractTagNames(const QString& expression) const;

    double evaluateExpression(
        const QString& expression,
        const QHash<QString, double>& variables
    );

    TagBus& m_bus;
    DbManager& m_db;

    AppConfig m_config;

    QJSEngine m_jsEngine;

    QHash<qint64, ComputedTag> m_computedTagsById;
    QHash<qint64, QSet<qint64>> m_dependencyMap;

    QHash<qint64, double> m_latestValues;
    QHash<qint64, QString> m_tagNames;

    QTimer m_periodicTimer;
};
#endif // COMPUTEDTAGENGINE_H
