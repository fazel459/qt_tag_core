#pragma once

#include <functional>
#include <vector>

#include <QString>

#include "../core/Models.h"

struct BusMessage
{
    QString topic;
    TagValue value;
};

class TagBus
{
public:
    using Handler = std::function<void(const BusMessage&)>;

    void subscribe(const QString& filter, Handler handler)
    {
        m_subscriptions.push_back({filter, std::move(handler)});
    }

    void publish(const QString& topic, const TagValue& value)
    {
        BusMessage message {topic, value};

        for (auto& subscription : m_subscriptions)
        {
            if (matches(subscription.filter, topic))
            {
                subscription.handler(message);
            }
        }
    }

private:
    struct Subscription
    {
        QString filter;
        Handler handler;
    };

    std::vector<Subscription> m_subscriptions;

    bool matches(const QString& filter, const QString& topic) const
    {
        if (filter == "#")
        {
            return true;
        }

        if (filter == topic)
        {
            return true;
        }

        if (filter.endsWith("/#"))
        {
            QString prefix = filter.left(filter.size() - 2);
            return topic == prefix || topic.startsWith(prefix + "/");
        }

        return false;
    }
};