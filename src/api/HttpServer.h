#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QJsonObject>
#include <functional>

struct HttpRequest {
    QString method;
    QString path;
    QHash<QString, QString> queryParams;
    QHash<QString, QString> headers;
    QByteArray body;
    QJsonObject jsonBody;

    QString queryParam(const QString& key, const QString& defaultValue = QString()) const {
        return queryParams.value(key, defaultValue);
    }
};

struct HttpResponse {
    int statusCode = 200;
    QJsonObject jsonBody;
    QString errorMessage;

    static HttpResponse ok(const QJsonObject& body = QJsonObject()) {
        HttpResponse res;
        res.statusCode = 200;
        res.jsonBody = body;
        return res;
    }

    static HttpResponse created(const QJsonObject& body = QJsonObject()) {
        HttpResponse res;
        res.statusCode = 201;
        res.jsonBody = body;
        return res;
    }

    static HttpResponse badRequest(const QString& message) {
        HttpResponse res;
        res.statusCode = 400;
        res.errorMessage = message;
        return res;
    }

    static HttpResponse notFound(const QString& message = "Not found") {
        HttpResponse res;
        res.statusCode = 404;
        res.errorMessage = message;
        return res;
    }

    static HttpResponse serverError(const QString& message) {
        HttpResponse res;
        res.statusCode = 500;
        res.errorMessage = message;
        return res;
    }
};

class HttpServer : public QObject
{
    Q_OBJECT

public:
    using RequestHandler = std::function<HttpResponse(const HttpRequest&)>;

    explicit HttpServer(QObject *parent = nullptr);
    ~HttpServer() override;

    bool start(const QString& host, int port);
    void stop();
    bool isListening() const;
    int serverPort() const;

    void setRequestHandler(RequestHandler handler);

signals:
    void started(const QString& host, int port);
    void failed(const QString& message);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    QTcpServer* m_server = nullptr;
    QHash<QTcpSocket*, QByteArray> m_pendingData;
    RequestHandler m_handler;

    void parseRequest(QTcpSocket* socket, const QByteArray& data);
    void sendResponse(QTcpSocket* socket, const HttpResponse& response);
};

#endif // HTTPSERVER_H
