QT += core sql
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = tag_core
TEMPLATE = app

INCLUDEPATH += src

SOURCES += \
    src/main.cpp \
    src/app/CoreApplication.cpp \
    src/core/ConfigLoader.cpp \
    src/storage/DbManager.cpp \
    src/drivers/SimulatorDriver.cpp \
    src/rules/RuleEngine.cpp

HEADERS += \
    src/app/CoreApplication.h \
    src/core/Models.h \
    src/core/ConfigLoader.h \
    src/tagbus/TagBus.h \
    src/storage/DbManager.h \
    src/storage/HistorianWriter.h \
    src/realtime/RealtimeCache.h \
    src/ingestion/DeadbandFilter.h \
    src/scaling/ScalingEngine.h \
    src/drivers/SimulatorDriver.h \
    src/rules/RuleEngine.h
	