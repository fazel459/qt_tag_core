#pragma once

#include <QSqlDatabase>

#include "../core/Models.h"

class DbManager
{
public:
    bool initialize(const AppConfig& cfg);

    bool upsertTag(const TagDefinition& tag);

    bool insertRaw(const TagValue& value);
    bool upsertCurrent(const TagValue& value);

    bool insertAlarm(
            qint64 tagId,
            const QString& ruleType,
            const QString& severity,
            double value,
            double threshold,
            const QString& message
            );

    bool clearActiveAlarms(qint64 tagId, const QString& ruleType);
    bool writeBatch(const QVector<TagValue>& rawValues,const QVector<TagValue>& latestValues);

    bool insertRawBatch(const QVector<TagValue>& rawValues);
    bool upsertCurrentBatch(const QVector<TagValue>& latestValues);

private:
    bool migrate();

    static QString sourceToString(SourceKind source);

    QSqlDatabase m_db;
};
