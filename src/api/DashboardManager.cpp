#include "DashboardManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDebug>
#include <QDirIterator>
#include <QRegularExpression>

DashboardManager::DashboardManager(DbManager& db, const QString& basePath, QObject* parent)
    : QObject(parent)
    , m_db(db)
    , m_basePath(basePath)
{
    // ساخت directory اصلی اگر وجود ندارد
    QDir dir(m_basePath);
    if (!dir.exists()) {
        dir.mkpath(".");
        qInfo() << "Dashboard base path created:" << m_basePath;
    }
}

DashboardManager::~DashboardManager()
{
}

// ============================================================
// CRUD Operations
// ============================================================

QVector<DashboardDefinition> DashboardManager::listDashboards()
{
    return m_db.loadDashboards();
}

DashboardDefinition DashboardManager::getDashboard(qint64 dashboardId)
{
    return m_db.loadDashboard(dashboardId);
}

qint64 DashboardManager::createDashboard(const DashboardDefinition& dashboard)
{
    // Validation
    if (dashboard.name.isEmpty()) {
        qWarning() << "Dashboard name is empty";
        return -1;
    }

    // Insert در دیتابیس
    const qint64 newId = m_db.insertDashboard(dashboard);
    if (newId <= 0) {
        qWarning() << "Failed to insert dashboard into database";
        return -1;
    }

    // ساخت directory و فایل‌های اولیه
    if (!ensureDashboardDir(newId)) {
        qWarning() << "Failed to create dashboard directory:" << newId;
        // rollback: حذف از دیتابیس
        m_db.deleteDashboard(newId);
        return -1;
    }

    // ذخیره content اولیه
    QString initialContent;
    if (dashboard.dashboardType == "qml") {
        initialContent = QStringLiteral(
            "import QtQuick 2.15\n"
            "import QtQuick.Controls 2.15\n"
            "\n"
            "Rectangle {\n"
            "    width: 800\n"
            "    height: 600\n"
            "    color: '#1e1e1e'\n"
            "\n"
            "    Text {\n"
            "        text: 'New Dashboard'\n"
            "        color: '#ffffff'\n"
            "        anchors.centerIn: parent\n"
            "        font.pixelSize: 24\n"
            "    }\n"
            "}\n"
        );
    } else if (dashboard.dashboardType == "html") {
        initialContent = QStringLiteral(
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head>\n"
            "    <title>New Dashboard</title>\n"
            "    <style>\n"
            "        body { background: #1e1e1e; color: #fff; }\n"
            "    </style>\n"
            "</head>\n"
            "<body>\n"
            "    <h1>New Dashboard</h1>\n"
            "</body>\n"
            "</html>\n"
        );
    } else {
        // simple: JSON config
        initialContent = QStringLiteral(
            "{\n"
            "  \"version\": \"1.0\",\n"
            "  \"type\": \"simple\",\n"
            "  \"layout\": {\"type\": \"grid\", \"columns\": 4, \"rows\": 3},\n"
            "  \"widgets\": [],\n"
            "  \"refresh_interval_ms\": 1000,\n"
            "  \"tag_ids\": []\n"
            "}\n"
        );
    }

    if (!saveDashboardContent(newId, initialContent)) {
        qWarning() << "Failed to save initial dashboard content:" << newId;
    }

    qInfo() << "Dashboard created:" << newId << dashboard.name
            << "type:" << dashboard.dashboardType;
    return newId;
}

bool DashboardManager::updateDashboard(const DashboardDefinition& dashboard)
{
    if (dashboard.dashboardId <= 0) {
        return false;
    }

    // بررسی وجود داشبورد
    DashboardDefinition existing = m_db.loadDashboard(dashboard.dashboardId);
    if (existing.dashboardId == 0) {
        return false;
    }

    return m_db.updateDashboard(dashboard);
}

bool DashboardManager::deleteDashboard(qint64 dashboardId)
{
    // حذف از دیتابیس
    if (!m_db.deleteDashboard(dashboardId)) {
        return false;
    }

    // حذف directory و فایل‌ها
    QString dirPath = dashboardDir(dashboardId);
    QDir dir(dirPath);
    if (dir.exists()) {
        if (!dir.removeRecursively()) {
            qWarning() << "Failed to delete dashboard directory:" << dirPath;
        }
    }

    qInfo() << "Dashboard deleted:" << dashboardId;
    return true;
}

// ============================================================
// Content Operations (File-based)
// ============================================================

QString DashboardManager::getDashboardContent(qint64 dashboardId)
{
    DashboardDefinition dashboard = m_db.loadDashboard(dashboardId);
    if (dashboard.dashboardId == 0) {
        return QString();
    }

    QString filePath = contentFilePath(dashboardId, dashboard.dashboardType);
    QFile file(filePath);

    if (!file.exists()) {
        qWarning() << "Dashboard content file not found:" << filePath;
        return QString();
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open dashboard content:" << filePath;
        return QString();
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    const QString content = stream.readAll();
    file.close();

    return content;
}

bool DashboardManager::saveDashboardContent(qint64 dashboardId, const QString& content)
{
    DashboardDefinition dashboard = m_db.loadDashboard(dashboardId);
    if (dashboard.dashboardId == 0) {
        qWarning() << "Dashboard not found:" << dashboardId;
        return false;
    }

    // Validation
    QString error;
    if (!validateDashboardContent(dashboardId, content, error)) {
        qWarning() << "Dashboard content validation failed:" << error;
        return false;
    }

    // مطمئن شو directory وجود دارد
    if (!ensureDashboardDir(dashboardId)) {
        qWarning() << "Failed to ensure dashboard directory:" << dashboardId;
        return false;
    }

    QString filePath = contentFilePath(dashboardId, dashboard.dashboardType);
    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning() << "Failed to open file for writing:" << filePath;
        return false;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << content;
    file.close();

    // آپدیت updated_at در دیتابیس
    m_db.touchDashboard(dashboardId);

    qInfo() << "Dashboard content saved:" << dashboardId << filePath;
    return true;
}

// ============================================================
// Resource Operations
// ============================================================

QByteArray DashboardManager::getResource(qint64 dashboardId, const QString& resourcePath)
{
    QString safePath = sanitizePath(resourcePath);
    if (safePath.isEmpty()) {
        qWarning() << "Invalid resource path:" << resourcePath;
        return QByteArray();
    }

    QString filePath = dashboardDir(dashboardId) + "/resources/" + safePath;
    QFile file(filePath);

    if (!file.exists()) {
        return QByteArray();
    }

    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }

    QByteArray data = file.readAll();
    file.close();
    return data;
}

bool DashboardManager::saveResource(qint64 dashboardId, const QString& resourcePath, const QByteArray& data)
{
    QString safePath = sanitizePath(resourcePath);
    if (safePath.isEmpty()) {
        qWarning() << "Invalid resource path:" << resourcePath;
        return false;
    }

    QString filePath = dashboardDir(dashboardId) + "/resources/" + safePath;

    // ساخت directory های میانی
    QFileInfo fileInfo(filePath);
    QDir().mkpath(fileInfo.absolutePath());

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Failed to open resource file for writing:" << filePath;
        return false;
    }

    file.write(data);
    file.close();

    return true;
}

QStringList DashboardManager::listResources(qint64 dashboardId)
{
    QStringList resources;
    QString resourcesDir = dashboardDir(dashboardId) + "/resources";

    QDir dir(resourcesDir);
    if (!dir.exists()) {
        return resources;
    }

    QDirIterator it(resourcesDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QString relativePath = QDir(resourcesDir).relativeFilePath(it.filePath());
        resources.append(relativePath);
    }

    return resources;
}

// ============================================================
// Validation
// ============================================================

bool DashboardManager::validateDashboardContent(qint64 dashboardId, const QString& content, QString& error)
{
    Q_UNUSED(dashboardId)

    if (content.isEmpty()) {
        error = "Content is empty";
        return false;
    }

    DashboardDefinition dashboard = m_db.loadDashboard(dashboardId);

    if (dashboard.dashboardType == "qml") {
        return validateQmlSyntax(content, error);
    } else if (dashboard.dashboardType == "simple") {
        return validateJsonConfig(content, error);
    } else if (dashboard.dashboardType == "html") {
        // Basic HTML validation
        if (!content.contains("<html") && !content.contains("<!DOCTYPE")) {
            error = "Invalid HTML: missing <html> or <!DOCTYPE>";
            return false;
        }
        // بررسی تگ‌های خطرناک
        if (content.contains("<script src=\"http", Qt::CaseInsensitive)) {
            qWarning() << "Warning: external script detected in HTML dashboard";
        }
        return true;
    }

    error = "Unknown dashboard type: " + dashboard.dashboardType;
    return false;
}

bool DashboardManager::validateQmlSyntax(const QString& qmlContent, QString& error)
{
    // Validation ساده QML
    // Qt 5.14 API مستقیمی برای syntax check ندارد
    // پس validation اولیه انجام می‌دهیم

    if (qmlContent.isEmpty()) {
        error = "QML content is empty";
        return false;
    }

    // بررسی import
    if (!qmlContent.contains("import")) {
        error = "QML must contain at least one import statement";
        return false;
    }

    // بررسی bracket balance
    int braceCount = 0;
    for (QChar c : qmlContent) {
        if (c == '{') braceCount++;
        else if (c == '}') braceCount--;
        if (braceCount < 0) {
            error = "Unbalanced braces in QML";
            return false;
        }
    }
    if (braceCount != 0) {
        error = "Unbalanced braces in QML";
        return false;
    }

    // بررسی کلمات کلیدی خطرناک (امنیت)
    QStringList dangerousPatterns = {
        "Qt.createQmlObject",
        "import QtQml.Models",
        "XMLHttpRequest"
    };

    for (const QString& pattern : dangerousPatterns) {
        if (qmlContent.contains(pattern)) {
            qWarning() << "Warning: potentially dangerous QML pattern:" << pattern;
        }
    }

    return true;
}

bool DashboardManager::validateJsonConfig(const QString& jsonContent, QString& error)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonContent.toUtf8(), &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        error = "JSON parse error: " + parseError.errorString();
        return false;
    }

    if (!doc.isObject()) {
        error = "JSON config must be an object";
        return false;
    }

    QJsonObject obj = doc.object();

    // بررسی فیلدهای اجباری
    if (!obj.contains("version")) {
        error = "Missing 'version' field in config";
        return false;
    }

    if (!obj.contains("type")) {
        error = "Missing 'type' field in config";
        return false;
    }

    return true;
}

// ============================================================
// Helpers
// ============================================================

QString DashboardManager::dashboardDir(qint64 dashboardId) const
{
    return m_basePath + "/" + QString::number(dashboardId);
}

QString DashboardManager::contentFilePath(qint64 dashboardId, const QString& type) const
{
    return dashboardDir(dashboardId) + "/" + contentFileName(type);
}

QString DashboardManager::contentFileName(const QString& type) const
{
    if (type == "qml") {
        return "dashboard.qml";
    } else if (type == "html") {
        return "dashboard.html";
    } else {
        return "dashboard.json";
    }
}

bool DashboardManager::ensureDashboardDir(qint64 dashboardId)
{
    QString dirPath = dashboardDir(dashboardId);
    QDir dir;

    if (!dir.exists(dirPath)) {
        if (!dir.mkpath(dirPath)) {
            return false;
        }
    }

    // ساخت resources directory
    QString resourcesPath = dirPath + "/resources";
    if (!dir.exists(resourcesPath)) {
        dir.mkpath(resourcesPath);
    }

    return true;
}

QString DashboardManager::sanitizePath(const QString& path) const
{
    // جلوگیری از path traversal
    if (path.contains("..")) {
        return QString();
    }

    if (path.startsWith("/") || path.startsWith("\\")) {
        return QString();
    }

    // فقط کاراکترهای مجاز
    QRegularExpression re("^[a-zA-Z0-9_\\-./]+$");
    if (!re.match(path).hasMatch()) {
        return QString();
    }

    return path;
}

