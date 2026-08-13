#include "UserManager.h"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QUuid>
#include <QDebug>

UserManager::UserManager(DbManager& db, QObject* parent)
    : QObject(parent), m_db(db)
{
}

void UserManager::ensureDefaultAdmin()
{
    if (m_db.userCount() > 0) return;
    const QString salt = generateSalt();
    const QString hash = hashPassword("admin123", salt);
    m_db.insertUserRaw("admin", hash, salt, "System Administrator", "admin");
    qInfo() << "[Auth] Default admin created (admin / admin123)";
}

QString UserManager::hashPassword(const QString& password, const QString& salt)
{
    QByteArray data = (salt + password).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString UserManager::generateSalt()
{
    QByteArray bytes;
    for (int i = 0; i < 16; ++i)
        bytes.append(char(QRandomGenerator::global()->bounded(256)));
    return QString::fromLatin1(bytes.toHex());
}

QString UserManager::generateToken()
{
    QString t = QUuid::createUuid().toString();
    t.remove('{').remove('}').remove('-');
    return t;
}

QJsonObject UserManager::login(const QString& username, const QString& password)
{
    QJsonObject result;
    UserDefinition user = m_db.loadUserByUsername(username);

    if (user.userId == 0) {
        result.insert("ok", false);
        result.insert("error", "Invalid username or password");
        return result;
    }
    if (!user.isActive) {
        result.insert("ok", false);
        result.insert("error", "User is disabled");
        return result;
    }

    const QString hash = hashPassword(password, user.salt);
    if (hash != user.passwordHash) {
        result.insert("ok", false);
        result.insert("error", "Invalid username or password");
        return result;
    }

    const QString token = generateToken();
    Session s;
    s.userId = user.userId;
    s.expiresAt = QDateTime::currentDateTimeUtc().addSecs(8 * 3600);
    m_sessions.insert(token, s);

    m_db.touchLastLogin(user.userId);

    QJsonObject data;
    data.insert("token", token);
    data.insert("user_id", user.userId);
    data.insert("username", user.username);
    data.insert("display_name", user.displayName);
    data.insert("role", user.role);
    data.insert("expires_in", 8 * 3600);

    result.insert("ok", true);
    result.insert("data", data);
    qInfo() << "[Auth] User logged in:" << username;
    return result;
}

bool UserManager::logout(const QString& token)
{
    return m_sessions.remove(token) > 0;
}

QJsonObject UserManager::me(const QString& token)
{
    QJsonObject result;
    UserDefinition user;
    if (!validateToken(token, user)) {
        result.insert("ok", false);
        result.insert("error", "Invalid or expired token");
        return result;
    }
    QJsonObject data;
    data.insert("user_id", user.userId);
    data.insert("username", user.username);
    data.insert("display_name", user.displayName);
    data.insert("role", user.role);
    result.insert("ok", true);
    result.insert("data", data);
    return result;
}

bool UserManager::validateToken(const QString& token, UserDefinition& outUser)
{
    if (!m_sessions.contains(token)) return false;
    Session s = m_sessions.value(token);
    if (s.expiresAt < QDateTime::currentDateTimeUtc()) {
        m_sessions.remove(token);
        return false;
    }
    UserDefinition u = m_db.loadUserById(s.userId);
    if (u.userId == 0 || !u.isActive) return false;
    outUser = u;
    return true;
}

bool UserManager::can(const UserDefinition& user, const QString& method, const QString& path) const
{
    if (user.role == "admin") return true;

    // مدیریت کاربران فقط admin
    if (path.startsWith("/api/v1/users")) return false;

    if (user.role == "operator") return true;

    if (user.role == "viewer")
        return (method == "GET" || method == "OPTIONS");

    return false;
}
