QT += core sql network
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = tag_core
TEMPLATE = app

INCLUDEPATH += src

SOURCES += \
    src/drivers/DriverFactory.cpp \
    src/drivers/DriverManager.cpp \
    src/drivers/ModbusTcpDriver.cpp \
    src/filters/FilterProcessor.cpp \
    src/filters/SoftwareFilterFactory.cpp \
    src/main.cpp \
    src/app/CoreApplication.cpp \
    src/core/ConfigLoader.cpp \
    src/storage/BatchHistorianWriter.cpp \
    src/storage/CurrentStateWriter.cpp \
    src/storage/DbManager.cpp \
    src/drivers/SimulatorDriver.cpp \
    src/rules/RuleEngine.cpp \
    src/storage/StorageExceptionFilter.cpp

HEADERS += \
    src/app/CoreApplication.h \
    src/core/Models.h \
    src/core/ConfigLoader.h \
    src/core/ValueUtils.h \
    src/drivers/DriverFactory.h \
    src/drivers/DriverManager.h \
    src/drivers/ITagDriver.h \
    src/drivers/ModbusTcpDriver.h \
    src/drivers/SimulatorDriverAdapter.h \
    src/filters/FilterProcessor.h \
    src/filters/ISoftwareFilter.h \
    src/filters/SoftwareFilterFactory.h \
    src/filters/SoftwareFilters.h \
    src/storage/BatchHistorianWriter.h \
    src/storage/CurrentStateWriter.h \
    src/storage/StorageExceptionFilter.h \
    src/tagbus/TagBus.h \
    src/storage/DbManager.h \
    src/storage/HistorianWriter.h \
    src/realtime/RealtimeCache.h \
    src/ingestion/DeadbandFilter.h \
    src/scaling/ScalingEngine.h \
    src/drivers/SimulatorDriver.h \
    src/rules/RuleEngine.h
	
