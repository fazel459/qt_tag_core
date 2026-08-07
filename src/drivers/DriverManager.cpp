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
}
