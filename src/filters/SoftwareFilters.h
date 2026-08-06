#ifndef SOFTWAREFILTERS_H
#define SOFTWAREFILTERS_H
#pragma once

#include <QDateTime>
#include <QQueue>
#include <QVector>

#include <algorithm>
#include <cmath>

#include "ISoftwareFilter.h"

class NoneFilter : public ISoftwareFilter
{
public:
    void reset() override
    {
    }

    double apply(double newValue, const QDateTime& /*timestamp*/) override
    {
        return newValue;
    }

    QString name() const override
    {
        return QStringLiteral("none");
    }
};

class MovingAverageFilter : public ISoftwareFilter
{
public:
    explicit MovingAverageFilter(int window = 5)
        : m_window(window)
    {
        if (m_window <= 0)
        {
            m_window = 5;
        }
    }

    void reset() override
    {
        m_values.clear();
        m_sum = 0.0;
    }

    double apply(double newValue, const QDateTime& /*timestamp*/) override
    {
        m_values.enqueue(newValue);
        m_sum += newValue;

        if (m_values.size() > m_window)
        {
            m_sum -= m_values.dequeue();
        }

        if (m_values.isEmpty())
        {
            return newValue;
        }

        return m_sum / static_cast<double>(m_values.size());
    }

    QString name() const override
    {
        return QStringLiteral("moving_average");
    }

private:
    int m_window = 5;
    QQueue<double> m_values;
    double m_sum = 0.0;
};

class ExponentialAverageFilter : public ISoftwareFilter
{
public:
    explicit ExponentialAverageFilter(double alpha = 0.2)
        : m_alpha(alpha)
    {
        if (m_alpha <= 0.0)
        {
            m_alpha = 0.000001;
        }

        if (m_alpha > 1.0)
        {
            m_alpha = 1.0;
        }
    }

    void reset() override
    {
        m_initialized = false;
        m_lastValue = 0.0;
    }

    double apply(double newValue, const QDateTime& /*timestamp*/) override
    {
        if (!m_initialized)
        {
            m_initialized = true;
            m_lastValue = newValue;
            return m_lastValue;
        }

        m_lastValue = m_alpha * newValue + (1.0 - m_alpha) * m_lastValue;

        return m_lastValue;
    }

    QString name() const override
    {
        return QStringLiteral("exponential_average");
    }

private:
    double m_alpha = 0.2;
    bool m_initialized = false;
    double m_lastValue = 0.0;
};

class MedianFilter : public ISoftwareFilter
{
public:
    explicit MedianFilter(int window = 5)
        : m_window(window)
    {
        if (m_window <= 0)
        {
            m_window = 5;
        }
    }

    void reset() override
    {
        m_values.clear();
    }

    double apply(double newValue, const QDateTime& /*timestamp*/) override
    {
        m_values.enqueue(newValue);

        if (m_values.size() > m_window)
        {
            m_values.dequeue();
        }

        if (m_values.isEmpty())
        {
            return newValue;
        }

        QVector<double> sorted;
        sorted.reserve(m_values.size());

        for (double value : m_values)
        {
            sorted.append(value);
        }

        std::sort(sorted.begin(), sorted.end());

        const int count = sorted.size();

        if (count % 2 == 1)
        {
            return sorted[count / 2];
        }

        return (sorted[count / 2 - 1] + sorted[count / 2]) / 2.0;
    }

    QString name() const override
    {
        return QStringLiteral("median");
    }

private:
    int m_window = 5;
    QQueue<double> m_values;
};

class DebounceFilter : public ISoftwareFilter
{
public:
    DebounceFilter(int debounceMs = 1000, double epsilon = 0.01)
        : m_debounceMs(debounceMs)
        , m_epsilon(epsilon)
    {
        if (m_debounceMs < 0)
        {
            m_debounceMs = 0;
        }

        if (m_epsilon < 0.0)
        {
            m_epsilon = 0.0;
        }
    }

    void reset() override
    {
        m_initialized = false;
        m_hasPending = false;
        m_lastOutput = 0.0;
        m_pendingValue = 0.0;
        m_pendingSince = QDateTime();
    }

    double apply(double newValue, const QDateTime& timestamp) override
    {
        if (!m_initialized)
        {
            m_initialized = true;
            m_lastOutput = newValue;
            return m_lastOutput;
        }

        if (std::fabs(newValue - m_lastOutput) <= m_epsilon)
        {
            m_hasPending = false;
            return m_lastOutput;
        }

        if (!m_hasPending || std::fabs(newValue - m_pendingValue) > m_epsilon)
        {
            m_pendingValue = newValue;
            m_pendingSince = timestamp;
            m_hasPending = true;
            return m_lastOutput;
        }

        if (m_pendingSince.isValid() &&
            m_pendingSince.msecsTo(timestamp) >= m_debounceMs)
        {
            m_lastOutput = newValue;
            m_hasPending = false;
            return m_lastOutput;
        }

        return m_lastOutput;
    }

    QString name() const override
    {
        return QStringLiteral("debounce");
    }

private:
    int m_debounceMs = 1000;
    double m_epsilon = 0.01;

    bool m_initialized = false;
    bool m_hasPending = false;

    double m_lastOutput = 0.0;
    double m_pendingValue = 0.0;

    QDateTime m_pendingSince;
};

class OutlierRejectionFilter : public ISoftwareFilter
{
public:
    OutlierRejectionFilter(double maxDelta = 5.0, int acceptAfterNOutliers = 3)
        : m_maxDelta(maxDelta)
        , m_acceptAfterNOutliers(acceptAfterNOutliers)
    {
        if (m_maxDelta < 0.0)
        {
            m_maxDelta = 0.0;
        }

        if (m_acceptAfterNOutliers <= 0)
        {
            m_acceptAfterNOutliers = 1;
        }
    }

    void reset() override
    {
        m_initialized = false;
        m_lastAcceptedValue = 0.0;
        m_consecutiveOutliers = 0;
    }

    double apply(double newValue, const QDateTime& /*timestamp*/) override
    {
        if (!m_initialized)
        {
            m_initialized = true;
            m_lastAcceptedValue = newValue;
            return newValue;
        }

        if (std::fabs(newValue - m_lastAcceptedValue) <= m_maxDelta)
        {
            m_consecutiveOutliers = 0;
            m_lastAcceptedValue = newValue;
            return newValue;
        }

        ++m_consecutiveOutliers;

        if (m_consecutiveOutliers >= m_acceptAfterNOutliers)
        {
            m_consecutiveOutliers = 0;
            m_lastAcceptedValue = newValue;
            return newValue;
        }

        return m_lastAcceptedValue;
    }

    QString name() const override
    {
        return QStringLiteral("outlier_rejection");
    }

private:
    double m_maxDelta = 5.0;
    int m_acceptAfterNOutliers = 3;

    bool m_initialized = false;
    double m_lastAcceptedValue = 0.0;
    int m_consecutiveOutliers = 0;
};

class RateLimiterFilter : public ISoftwareFilter
{
public:
    explicit RateLimiterFilter(double maxRatePerSecond = 1.0)
        : m_maxRatePerSecond(maxRatePerSecond)
    {
        if (m_maxRatePerSecond < 0.0)
        {
            m_maxRatePerSecond = 0.0;
        }
    }

    void reset() override
    {
        m_initialized = false;
        m_lastOutput = 0.0;
        m_lastTimestamp = QDateTime();
    }

    double apply(double newValue, const QDateTime& timestamp) override
    {
        if (!m_initialized)
        {
            m_initialized = true;
            m_lastOutput = newValue;
            m_lastTimestamp = timestamp;
            return newValue;
        }

        if (m_maxRatePerSecond <= 0.0)
        {
            m_lastOutput = newValue;
            m_lastTimestamp = timestamp;
            return newValue;
        }

        double dtSeconds = 0.0;

        if (m_lastTimestamp.isValid() && timestamp >= m_lastTimestamp)
        {
            dtSeconds = static_cast<double>(m_lastTimestamp.msecsTo(timestamp)) / 1000.0;
        }

        if (dtSeconds <= 0.0)
        {
            m_lastTimestamp = timestamp;
            return m_lastOutput;
        }

        const double maxDelta = m_maxRatePerSecond * dtSeconds;

        double difference = newValue - m_lastOutput;

        if (difference > maxDelta)
        {
            difference = maxDelta;
        }
        else if (difference < -maxDelta)
        {
            difference = -maxDelta;
        }

        m_lastOutput += difference;
        m_lastTimestamp = timestamp;

        return m_lastOutput;
    }

    QString name() const override
    {
        return QStringLiteral("rate_limiter");
    }

private:
    double m_maxRatePerSecond = 1.0;

    bool m_initialized = false;
    double m_lastOutput = 0.0;
    QDateTime m_lastTimestamp;
};
#endif // SOFTWAREFILTERS_H
