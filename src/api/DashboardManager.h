#ifndef DASHBOARDMANAGER_H
#define DASHBOARDMANAGER_H

#include <QObject>
#include <QString>
#include <QVector>
#include "../core/Models.h"
#include "../storage/DbManager.h"

class DashboardManager : public QObject
{
    Q_OBJECT

public:
    explicit DashboardManager(DbManager& db, const QString& basePath, QObject* parent = nullptr);
    ~DashboardManager() override;

    // CRUD operations
    QVector<DashboardDefinition> listDashboards();
    DashboardDefinition getDashboard(qint64 dashboardId);
    qint64 createDashboard(const DashboardDefinition& dashboard);
    bool updateDashboard(const DashboardDefinition& dashboard);
    bool deleteDashboard(qint64 dashboardId);

    // Content operations (file-based)
    QString getDashboardContent(qint64 dashboardId);
    bool saveDashboardContent(qint64 dashboardId, const QString& content);

    // Resource operations
    QByteArray getResource(qint64 dashboardId, const QString& resourcePath);
    bool saveResource(qint64 dashboardId, const QString& resourcePath, const QByteArray& data);
    QStringList listResources(qint64 dashboardId);

    // Validation
    bool validateDashboardContent(qint64 dashboardId, const QString& content, QString& error);
    bool validateQmlSyntax(const QString& qmlContent, QString& error);
    bool validateJsonConfig(const QString& jsonContent, QString& error);

    // Helpers
    QString dashboardDir(qint64 dashboardId) const;
    QString contentFilePath(qint64 dashboardId, const QString& type) const;

private:
    DbManager& m_db;
    QString m_basePath;

    bool ensureDashboardDir(qint64 dashboardId);
    QString sanitizePath(const QString& path) const;
    QString contentFileName(const QString& type) const;
};

#endif // DASHBOARDMANAGER_H
