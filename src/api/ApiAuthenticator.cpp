#include "ApiAuthenticator.h"

ApiAuthenticator::ApiAuthenticator()
{
    // مسیرهایی که همیشه بدون auth هستند
    m_config.publicPaths << QStringLiteral("/api/v1/health")
                         << QStringLiteral("/health")
                         << QStringLiteral("/api/v1/system/status")
                         << QStringLiteral("/api/v1/auth/login");
}

void ApiAuthenticator::setConfig(const Config& config)
{
    m_config = config;

    // همیشه مسیرهای عمومی را نگه دار
    if (!m_config.publicPaths.contains(QStringLiteral("/api/v1/health"))) {
        m_config.publicPaths << QStringLiteral("/api/v1/health")
                             << QStringLiteral("/health")
                             << QStringLiteral("/api/v1/system/status");
    }
}

bool ApiAuthenticator::isEnabled() const
{
    return m_config.enabled;
}

bool ApiAuthenticator::isPublicPath(const QString& path) const
{
    return m_config.publicPaths.contains(path);
}

QString ApiAuthenticator::extractApiKey(const HttpRequest& request) const
{
    // اول از header بخوان
    if (request.headers.contains(QStringLiteral("x-api-key"))) {
        return request.headers.value(QStringLiteral("x-api-key"));
    }

    // بعد از query parameter بخوان
    if (request.queryParams.contains(QStringLiteral("api_key"))) {
        return request.queryParams.value(QStringLiteral("api_key"));
    }

    return QString();
}

bool ApiAuthenticator::authenticate(const HttpRequest& request) const
{
    // اگر auth غیرفعال باشد، همه چیز قبول است (حالت development)
    if (!m_config.enabled) {
        return true;
    }

    // مسیرهای عمومی بدون auth
    if (isPublicPath(request.path)) {
        return true;
    }

    const QString apiKey = extractApiKey(request);
    if (apiKey.isEmpty()) {
        return false;
    }

    return m_config.apiKeys.contains(apiKey);
}
