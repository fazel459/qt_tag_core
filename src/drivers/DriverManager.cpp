#include "DriverManager.h"

#include <QDebug>
#include <QHash>

#include "DriverFactory.h"

DriverManager::DriverManager(TagBus& bus, const AppConfig& config)
    : m_bus(bus)
    , m_config(config)
{
}

DriverManager::~DriverManager()
{
    stopAll();

    qDeleteAll(m_drivers);
    m_drivers.clear();
    m_driverMap.clear();
}

bool DriverManager::startAll()
{
    qInfo() << "DriverManager: starting"
            << m_config.drivers.size()
            << "drivers";

    QHash<qint64, QVector<TagDefinition>> tagsByDriver;

    for (const TagDefinition& tag : m_config.tags)
    {
        tagsByDriver[tag.driverId].push_back(tag);
    }

    if (m_config.drivers.isEmpty())
    {
        qWarning() << "DriverManager: no drivers found in database";
        return false;
    }

    for (const DriverDefinition& driver : m_config.drivers)
    {
        if (!driver.enabled)
        {
            qInfo() << "Driver disabled:" << driver.name;
            continue;
        }

        const QVector<TagDefinition> tags = tagsByDriver.value(driver.driverId);

        qInfo() << "Creating driver:"
                << driver.name
                << "type:" << driver.type
                << "driverId:" << driver.driverId
                << "tags:" << tags.size();

        ITagDriver* createdDriver = DriverFactory::create(driver, tags, m_bus, m_config);

        if (createdDriver == nullptr)
        {
            qWarning() << "Failed to create driver:" << driver.name;
            continue;
        }

        m_drivers.push_back(createdDriver);
        m_driverMap[driver.driverId] = createdDriver;

        if (!createdDriver->start())
        {
            qWarning() << "Failed to start driver:" << driver.name;
        }
        else
        {
            qInfo() << "Driver started:"
                    << driver.name
                    << "type:" << driver.type
                    << "tags:" << tags.size();
        }
    }

    if (m_drivers.isEmpty())
    {
        qWarning() << "DriverManager: no driver was started";
        return false;
    }

    return true;
}


void DriverManager::stopAll()
{
    for (ITagDriver* driver : m_drivers)
    {
        driver->stop();
    }
    m_driverMap.clear();
}

bool DriverManager::startDriver(qint64 driverId)
{
    // اگر درایور قبلاً در map است
    if (m_driverMap.contains(driverId)) {
        ITagDriver* driver = m_driverMap.value(driverId);
        if (driver && driver->isConnected()) {
            qInfo() << "DriverManager: driver already running:" << driverId;
            return true;
        }
        if (driver && driver->start()) {
            qInfo() << "DriverManager: driver restarted:" << driverId;
            return true;
        }
        return false;
    }

    // پیدا کردن DriverDefinition از config
    DriverDefinition def;
    bool found = false;
    for (const DriverDefinition& d : m_config.drivers) {
        if (d.driverId == driverId) {
            def = d;
            found = true;
            break;
        }
    }

    if (!found) {
        qWarning() << "DriverManager: driver not found in config:" << driverId;
        return false;
    }

    if (!def.enabled) {
        qWarning() << "DriverManager: driver is disabled:" << driverId;
        return false;
    }

    // جمع کردن تگ‌های این درایور
    QVector<TagDefinition> tags;
    for (const TagDefinition& tag : m_config.tags) {
        if (tag.driverId == driverId) {
            tags.push_back(tag);
        }
    }

    // ساخت درایور جدید
    ITagDriver* newDriver = DriverFactory::create(def, tags, m_bus, m_config);
    if (!newDriver) {
        qWarning() << "DriverManager: failed to create driver:" << driverId;
        return false;
    }

    m_drivers.push_back(newDriver);
    m_driverMap[driverId] = newDriver;

    if (!newDriver->start()) {
        qWarning() << "DriverManager: failed to start driver:" << driverId;
        return false;
    }

    qInfo() << "DriverManager: driver started:" << driverId << "tags:" << tags.size();
    return true;
}

bool DriverManager::stopDriver(qint64 driverId)
{
    if (!m_driverMap.contains(driverId)) {
        qWarning() << "DriverManager: driver not found in active drivers:" << driverId;
        return false;
    }

    ITagDriver* driver = m_driverMap.value(driverId);
    if (driver) {
        driver->stop();
    }

    // حذف از map
    m_driverMap.remove(driverId);

    // حذف از m_drivers
    for (int i = 0; i < m_drivers.size(); ++i) {
        if (m_drivers[i] == driver) {
            m_drivers.removeAt(i);
            delete driver;  // ✅ آزاد کردن حافظه
            break;
        }
    }

    qInfo() << "DriverManager: driver stopped:" << driverId;
    return true;
}

bool DriverManager::isDriverRunning(qint64 driverId) const
{
    if (!m_driverMap.contains(driverId)) {
        return false;
    }
    ITagDriver* driver = m_driverMap.value(driverId);
    return driver && driver->isConnected();
}

QVector<qint64> DriverManager::runningDriverIds() const
{
    QVector<qint64> ids;
    for (auto it = m_driverMap.constBegin(); it != m_driverMap.constEnd(); ++it) {
        if (it.value() && it.value()->isConnected()) {
            ids.append(it.key());
        }
    }
    return ids;
}
