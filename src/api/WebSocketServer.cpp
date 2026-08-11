#include "WebSocketServer.h"
#include "WebSocketHandler.h"

#include <QWebSocketServer>
#include <QWebSocket>
#include <QHostAddress>

WebSocketServer::WebSocketServer(QObject *parent)
    : QObject(parent)
    , m_server(new QWebSocketServer(QStringLiteral("TagCoreWebSocketServer"),
                                    QWebSocketServer::NonSecureMode,
                                    this))
    , m_handler(new WebSocketHandler(this))
{
    connect(m_server, &QWebSocketServer::newConnection,
            this, &WebSocketServer::onNewConnection);
}

WebSocketServer::~WebSocketServer()
{
    stop();
}

bool WebSocketServer::start(const WebSocketServer::Config &config)
{
    m_config = config;

    if (!m_server || !m_handler) {
        emit failed(QStringLiteral("WebSocket server not initialized"));
        return false;
    }

    if (m_server->isListening()) {
        return true;
    }

    m_handler->setBatchInterval(config.batchIntervalMs);

    QHostAddress address;
    if (config.host == QStringLiteral("*") || config.host == QStringLiteral("0.0.0.0")) {
        address = QHostAddress(QHostAddress::Any);
    } else if (config.host == QStringLiteral("localhost") || config.host == QStringLiteral("127.0.0.1")) {
        address = QHostAddress(QHostAddress::LocalHost);
    } else {
        address = QHostAddress(config.host);
    }

    if (!m_server->listen(address, quint16(config.port))) {
        emit failed(QStringLiteral("WebSocket listen failed on %1:%2 - %3")
                        .arg(config.host)
                        .arg(config.port)
                        .arg(m_server->errorString()));
        return false;
    }

    emit started(address.toString(), int(m_server->serverPort()));
    return true;
}

void WebSocketServer::stop()
{
    if (m_server && m_server->isListening()) {
        m_server->close();
    }
}

bool WebSocketServer::isListening() const
{
    return m_server && m_server->isListening();
}

int WebSocketServer::serverPort() const
{
    return (m_server ? int(m_server->serverPort()) : 0);
}

WebSocketHandler* WebSocketServer::handler() const
{
    return m_handler;
}

void WebSocketServer::onNewConnection()
{
    if (!m_server || !m_handler) {
        return;
    }

    while (m_server->hasPendingConnections()) {
        QWebSocket *socket = m_server->nextPendingConnection();
        if (socket) {
            m_handler->addClient(socket);
        }
    }
}

