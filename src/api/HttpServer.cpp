#include "HttpServer.h"

#include <QJsonDocument>
#include <QUrlQuery>
#include <QUrl>
#include <QRegularExpression>
#include <QHostAddress>
#include <QAbstractSocket>

HttpServer::HttpServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection,
            this, &HttpServer::onNewConnection);
}

HttpServer::~HttpServer()
{
    stop();
}

void HttpServer::setRequestHandler(RequestHandler handler)
{
    m_handler = std::move(handler);
}

bool HttpServer::start(const QString& host, int port)
{
    if (m_server->isListening()) {
        return true;
    }

    QHostAddress address;
    if (host == QStringLiteral("*") || host == QStringLiteral("0.0.0.0")) {
        address = QHostAddress(QHostAddress::Any);
    } else if (host == QStringLiteral("localhost") || host == QStringLiteral("127.0.0.1")) {
        address = QHostAddress(QHostAddress::LocalHost);
    } else {
        address = QHostAddress(host);
    }

    if (!m_server->listen(address, quint16(port))) {
        emit failed(QStringLiteral("HTTP server listen failed on %1:%2 - %3")
                        .arg(host)
                        .arg(port)
                        .arg(m_server->errorString()));
        return false;
    }

    emit started(address.toString(), int(m_server->serverPort()));
    return true;
}

void HttpServer::stop()
{
    if (m_server && m_server->isListening()) {
        m_server->close();
    }
}

bool HttpServer::isListening() const
{
    return m_server && m_server->isListening();
}

int HttpServer::serverPort() const
{
    return m_server ? int(m_server->serverPort()) : 0;
}

void HttpServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();

        connect(socket, &QTcpSocket::readyRead,
                this, &HttpServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected,
                this, &HttpServer::onDisconnected);

        m_pendingData[socket] = QByteArray();
    }
}

void HttpServer::onReadyRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    m_pendingData[socket].append(socket->readAll());

    int headerEnd = m_pendingData[socket].indexOf("\r\n\r\n");
    if (headerEnd == -1) {
        return;
    }

    QByteArray headerPart = m_pendingData[socket].left(headerEnd);
    QString headerStr = QString::fromUtf8(headerPart);

    int contentLength = 0;
    QRegularExpression re("Content-Length:\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(headerStr);
    if (match.hasMatch()) {
        contentLength = match.captured(1).toInt();
    }

    int totalLength = headerEnd + 4 + contentLength;
    if (m_pendingData[socket].size() < totalLength) {
        return;
    }

    parseRequest(socket, m_pendingData[socket].left(totalLength));
    m_pendingData[socket].remove(0, totalLength);
}

void HttpServer::onDisconnected()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (socket) {
        m_pendingData.remove(socket);
        socket->deleteLater();
    }
}

void HttpServer::parseRequest(QTcpSocket* socket, const QByteArray& data)
{
    HttpRequest request;

    int headerEnd = data.indexOf("\r\n\r\n");
    QByteArray headerPart = data.left(headerEnd);
    QByteArray bodyPart = data.mid(headerEnd + 4);

    QList<QByteArray> lines = headerPart.split('\n');
    if (lines.isEmpty()) return;

    QList<QByteArray> firstLine = lines[0].trimmed().split(' ');
    if (firstLine.size() < 2) return;

    request.method = QString::fromUtf8(firstLine[0]).toUpper();
    QString fullPath = QString::fromUtf8(firstLine[1]);

    int queryPos = fullPath.indexOf('?');
    if (queryPos >= 0) {
        request.path = fullPath.left(queryPos);
        QUrlQuery query(fullPath.mid(queryPos + 1));
        // decode percent-encoding (مثل %2C و %3A) — dio این کاراکترها را کد میکند
        for (const auto& item : query.queryItems()) {
            request.queryParams[item.first] =
                QUrl::fromPercentEncoding(item.second.toUtf8());
        }
    } else {
        request.path = fullPath;
    }

    for (int i = 1; i < lines.size(); ++i) {
        QByteArray line = lines[i].trimmed();
        int colonPos = line.indexOf(':');
        if (colonPos > 0) {
            QString key = QString::fromUtf8(line.left(colonPos)).toLower();
            QString value = QString::fromUtf8(line.mid(colonPos + 1)).trimmed();
            request.headers[key] = value;
        }
    }

    request.body = bodyPart;
    if (!bodyPart.isEmpty()) {
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(bodyPart, &error);
        if (error.error == QJsonParseError::NoError && doc.isObject()) {
            request.jsonBody = doc.object();
        }
    }

    // ✅ صدا زدن handler و ارسال response
    HttpResponse response;
    if (m_handler) {
        response = m_handler(request);
    } else {
        response = HttpResponse::serverError("No handler configured");
    }

    sendResponse(socket, response);
}

void HttpServer::sendResponse(QTcpSocket* socket, const HttpResponse& response)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    QByteArray body;
    QString contentType = response.contentType;

    // اگر rawBody داشتیم (مثل CSV)، آن را بفرست
    if (!response.rawBody.isEmpty()) {
        body = response.rawBody;
        contentType = response.contentType;
    } else {
        // در غیر این صورت JSON بساز
        QJsonObject responseBody;

        if (response.statusCode >= 400) {
            responseBody.insert("error", response.errorMessage);
        } else {
            responseBody = response.jsonBody;
        }

        body = QJsonDocument(responseBody).toJson(QJsonDocument::Compact);
        contentType = "application/json";
    }

    QString statusText;
    switch (response.statusCode) {
    case 200: statusText = "OK"; break;
    case 201: statusText = "Created"; break;
    case 204: statusText = "No Content"; break;
    case 400: statusText = "Bad Request"; break;
    case 404: statusText = "Not Found"; break;
    case 405: statusText = "Method Not Allowed"; break;
    case 500: statusText = "Internal Server Error"; break;
    default: statusText = "Unknown"; break;
    }

    QByteArray httpResponse;
    httpResponse.append("HTTP/1.1 ");
    httpResponse.append(QByteArray::number(response.statusCode));
    httpResponse.append(" ");
    httpResponse.append(statusText.toUtf8());
    httpResponse.append("\r\n");
    httpResponse.append("Content-Type: ");
    httpResponse.append(contentType.toUtf8());
    httpResponse.append("\r\n");
    httpResponse.append("Content-Length: ");
    httpResponse.append(QByteArray::number(body.size()));
    httpResponse.append("\r\n");
    httpResponse.append("Access-Control-Allow-Origin: *\r\n");
    httpResponse.append("Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n");
    httpResponse.append("Access-Control-Allow-Headers: Content-Type\r\n");

    // ✅ جدید: Content-Disposition برای download
    if (!response.contentDisposition.isEmpty()) {
        httpResponse.append("Content-Disposition: ");
        httpResponse.append(response.contentDisposition.toUtf8());
        httpResponse.append("\r\n");
    }

    httpResponse.append("Connection: close\r\n");
    httpResponse.append("\r\n");
    httpResponse.append(body);

    socket->write(httpResponse);
    socket->flush();
    socket->disconnectFromHost();
}
