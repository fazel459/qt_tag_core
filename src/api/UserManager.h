#ifndef USERMANAGER_H
#define USERMANAGER_H

#include <QObject>
#include <QHash>
#include <QDateTime>
#include <QJsonObject>
#include "../core/Models.h"
#include "../storage/DbManager.h"

class UserManager : public QObject
{
    Q_OBJECT
public:
    explicit UserManager(DbManager& db, QObject* parent = nullptr);

    void ensureDefaultAdmin();

    QJsonObject login(const QString& username, const QString& password);
    bool logout(const QString& token);
    QJsonObject me(const QString& token);

    bool validateToken(const QString& token, UserDefinition& outUser);
    bool can(const UserDefinition& user, const QString& method, const QString& path) const;

    static QString hashPassword(const QString& password, const QString& salt);
    static QString generateSalt();
    static QString generateToken();

private:
    struct Session { qint64 userId = 0; QDateTime expiresAt; };
    DbManager& m_db;
    QHash<QString, Session> m_sessions;
};

#endif
