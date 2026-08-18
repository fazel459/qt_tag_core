#include "TagBus.h"
#include <QDebug>

TagBus::TagBus(QObject* parent) : QThread(parent)
{
}

TagBus::~TagBus()
{
    stop();
}

int TagBus::subscribe(const QString& topicFilter, Callback cb)
{
    QMutexLocker locker(&m_subMutex);
    Subscription s;
    s.id = m_nextId++;
    s.filter = topicFilter;
    s.cb = std::move(cb);
    m_subs.append(s);
    return s.id;
}

void TagBus::unsubscribe(int subscriptionId)
{
    QMutexLocker locker(&m_subMutex);
    for (int i = 0; i < m_subs.size(); ++i) {
        if (m_subs[i].id == subscriptionId) {
            m_subs.removeAt(i);
            return;
        }
    }
}

void TagBus::publish(const QString& topic, const TagValue& value)
{
    BusMessage msg;
    msg.topic = topic;
    msg.value = value;

    QMutexLocker locker(&m_queueMutex);
    m_queue.enqueue(msg);

    const int sz = m_queue.size();
    if (sz > 5000 && sz > m_highWaterMark) {
        m_highWaterMark = sz;
        qWarning() << "TagBus: queue growing:" << sz;
    }
    m_cond.wakeOne();
}

int TagBus::pendingCount() const
{
    QMutexLocker locker(&m_queueMutex);
    return m_queue.size();
}

void TagBus::stop()
{
    {
        QMutexLocker locker(&m_queueMutex);
        if (m_stopping) return;
        m_stopping = true;
        m_cond.wakeAll();
    }
    wait();
}

bool TagBus::matches(const QString& filter, const QString& topic)
{
    if (filter == topic) return true;
    if (filter == "#") return true;
    if (filter.endsWith("/#")) {
        const QString prefix = filter.left(filter.size() - 1);
        return topic.startsWith(prefix);
    }
    return false;
}

void TagBus::run()
{
    qInfo() << "TagBus dispatcher thread started";

    while (true) {
        QVector<BusMessage> batch;
        batch.reserve(256);

        {
            QMutexLocker locker(&m_queueMutex);
            while (m_queue.isEmpty()) {
                if (m_stopping) {
                    qInfo() << "TagBus dispatcher thread stopped";
                    return;
                }
                m_cond.wait(&m_queueMutex);
            }
            const int n = qMin(m_queue.size(), 256);
            for (int i = 0; i < n; ++i)
                batch.append(m_queue.dequeue());
            if (m_queue.size() < 1000) m_highWaterMark = 0;
        }

        QVector<Subscription> subs;
        {
            QMutexLocker locker(&m_subMutex);
            subs = m_subs;
        }

        for (const BusMessage& msg : batch) {
            for (const Subscription& s : subs) {
                if (s.cb && matches(s.filter, msg.topic)) {
                    s.cb(msg);   // اجرا روی thread dispatcher
                }
            }
        }
    }
}
