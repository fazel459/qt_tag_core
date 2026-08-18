#ifndef TAGBUS_H
#define TAGBUS_H

#include <QMutex>
#include <QQueue>
#include <QThread>
#include <QVector>
#include <QWaitCondition>
#include <functional>
#include "../core/Models.h"

struct BusMessage
{
    QString topic;
    TagValue value;
};

class TagBus : public QThread
{
    Q_OBJECT
public:
    using Callback = std::function<void(const BusMessage&)>;

    explicit TagBus(QObject* parent = nullptr);
    ~TagBus() override;

    int subscribe(const QString& topicFilter, Callback cb);
    void unsubscribe(int subscriptionId);

    // غیرمسدودکننده — هرگز داده را دور نمی‌ریزد
    void publish(const QString& topic, const TagValue& value);

    int pendingCount() const;
    void stop();

protected:
    void run() override;   // thread dispatcher

private:
    static bool matches(const QString& filter, const QString& topic);

    struct Subscription
    {
        int id = 0;
        QString filter;
        Callback cb;
    };

    mutable QMutex m_queueMutex;
    QQueue<BusMessage> m_queue;
    QWaitCondition m_cond;
    bool m_stopping = false;
    int m_highWaterMark = 0;

    QMutex m_subMutex;
    QVector<Subscription> m_subs;
    int m_nextId = 1;
};

#endif // TAGBUS_H
