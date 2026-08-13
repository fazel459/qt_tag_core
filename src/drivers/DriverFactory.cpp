#include "DriverFactory.h"

#include <QDebug>
#include "ModbusRtuDriver.h"
#include "ModbusTcpDriver.h"
#include "SimulatorDriverAdapter.h"
#include "MqttDriver.h"
#include "OpcUaDriver.h"

ITagDriver* DriverFactory::create(
    const DriverDefinition& driver,
    const QVector<TagDefinition>& tags,
    TagBus& bus,
    const AppConfig& config
)
{
    qInfo() << "DriverFactory: creating driver:"
            << driver.name
            << "type:" << driver.type
            << "tags:" << tags.size();
    if (driver.type == "simulator")
    {
        return new SimulatorDriverAdapter(bus, tags);
    }

    if (driver.type == "modbus_tcp")
    {
        return new ModbusTcpDriver(bus, driver, tags, config);
    }



    if (driver.type == "modbus_rtu")
    {
        return new ModbusRtuDriver(bus, driver, tags, config);
    }

    if (driver.type == "mqtt")
    {
        return new MqttDriver(bus, driver, tags, config);
    }

    if (driver.type == "opc_ua") {
        return new OpcUaDriver(bus, driver, tags, config);
    }

        qWarning() << "Unknown driver type:" << driver.type;
    return nullptr;
}
