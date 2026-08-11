#pragma once

#include <memory>

#include "../core/ConfigLoader.h"
#include "../core/Models.h"
#include "../drivers/SimulatorDriver.h"
#include "../realtime/RealtimeCache.h"
#include "../rules/RuleEngine.h"
#include "../storage/BatchHistorianWriter.h"
#include "../storage/CurrentStateWriter.h"
#include "../storage/DbManager.h"
#include "../storage/StorageExceptionFilter.h"
#include "../tagbus/TagBus.h"
#include "../filters/FilterProcessor.h"
#include "../drivers/DriverManager.h"
#include "../notifications/NotificationManager.h"
#include "../computed/ComputedTagEngine.h"
#include "../storage/HistorianManager.h"
#include "../storage/ArchiveManager.h"
#include "api/WebSocketServer.h"
//#include "api/WebSocketHandler.h"
#include <QObject>
class WebSocketServer;
class CoreApplication: public QObject
{
public:
    explicit CoreApplication(QObject *parent = nullptr);
    bool initialize();

private:
    static QString findConfigFile();

    AppConfig m_config;

    DbManager m_db;
    TagBus m_bus;
    void startApiLayer();

    WebSocketServer *m_wsServer = nullptr;

    std::unique_ptr<BatchHistorianWriter> m_historianWriter;
    std::unique_ptr<CurrentStateWriter> m_currentStateWriter;
    std::unique_ptr<StorageExceptionFilter> m_storageFilter;
    std::unique_ptr<RealtimeCache> m_realtimeCache;
    std::unique_ptr<RuleEngine> m_ruleEngine;
    std::unique_ptr<DriverManager> m_driverManager;
    std::unique_ptr<FilterProcessor> m_filterProcessor;
    QVector<DriverDefinition> drivers;
    std::unique_ptr<NotificationManager> m_notificationManager;
    std::unique_ptr<ComputedTagEngine> m_computedTagEngine;
    std::unique_ptr<HistorianManager> m_historianManager;
    std::unique_ptr<ArchiveManager> m_archiveManager;
};


