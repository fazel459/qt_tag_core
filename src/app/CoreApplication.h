#pragma once

#include <memory>

#include "../core/ConfigLoader.h"
#include "../core/Models.h"
#include "../drivers/SimulatorDriver.h"
#include "../realtime/RealtimeCache.h"
#include "../rules/RuleEngine.h"
#include "../storage/DbManager.h"
#include "../storage/HistorianWriter.h"
#include "../tagbus/TagBus.h"

class CoreApplication
{
public:
    bool initialize();

private:
    static QString findConfigFile();

    AppConfig m_config;

    DbManager m_db;
    TagBus m_bus;

    std::unique_ptr<HistorianWriter> m_historianWriter;
    std::unique_ptr<RealtimeCache> m_realtimeCache;
    std::unique_ptr<RuleEngine> m_ruleEngine;
    std::unique_ptr<SimulatorDriver> m_simulatorDriver;
};