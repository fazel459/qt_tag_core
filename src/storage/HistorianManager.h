#ifndef HISTORIANMANAGER_H
#define HISTORIANMANAGER_H
#pragma once

#include <QDateTime>
#include <QObject>
#include <QVector>

#include "../core/Models.h"
#include "DbManager.h"

struct AggregatePoint
{
    QDateTime bucket;
    qint64 tagId = 0;
    double avgValue = 0.0;
    double minValue = 0.0;
    double maxValue = 0.0;
    double firstValue = 0.0;
    double lastValue = 0.0;
    int sampleCount = 0;
};

class HistorianManager : public QObject
{
public:
    HistorianManager(DbManager& db, QObject* parent = nullptr);

    QVector<AggregatePoint> query1Min(
        qint64 tagId,
        const QDateTime& startTime,
        const QDateTime& endTime
    );

    QVector<AggregatePoint> query5Min(
        qint64 tagId,
        const QDateTime& startTime,
        const QDateTime& endTime
    );

    QVector<AggregatePoint> query1Hour(
        qint64 tagId,
        const QDateTime& startTime,
        const QDateTime& endTime
    );

    QVector<AggregatePoint> query1Day(
        qint64 tagId,
        const QDateTime& startTime,
        const QDateTime& endTime
    );


    void refreshContinuousAggregates();

    void refreshContinuousAggregate(const QString& viewName);

    void refreshContinuousAggregate(
        const QString& viewName,
        const QDateTime& startTime,
        const QDateTime& endTime
    );



private:
    QVector<AggregatePoint> queryAggregate(
        const QString& viewName,
        qint64 tagId,
        const QDateTime& startTime,
        const QDateTime& endTime
    );

    DbManager& m_db;
};

#endif // HISTORIANMANAGER_H
