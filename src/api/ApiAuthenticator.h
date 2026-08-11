#ifndef APIAUTHENTICATOR_H
#define APIAUTHENTICATOR_H

#include <QString>
#include <QStringList>
#include "HttpServer.h"

class ApiAuthenticator
{
public:
    struct Config {
        // پیش‌فرض غیرفعال است تا در تست‌ها مشکلی نباشد
        bool enabled = false;
        QStringList apiKeys;
        QStringList publicPaths;
    };

    ApiAuthenticator();

    void setConfig(const Config& config);
    bool isEnabled() const;

    bool isPublicPath(const QString& path) const;
    bool authenticate(const HttpRequest& request) const;
    QString extractApiKey(const HttpRequest& request) const;

private:
    Config m_config;

};

#endif // APIAUTHENTICATOR_H
