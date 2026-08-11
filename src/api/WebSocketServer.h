#ifndef WEBSOCKETSERVER_H
#define WEBSOCKETSERVER_H

#include <QObject>
#include <QString>
#include <functional>

class QWebSocketServer;
class WebSocketHandler;

class WebSocketServer : public QObject
{
    Q_OBJECT

public:
    struct Config {
        bool enabled = true;
        QString host = QStringLiteral("0.0.0.0");
        int port = 8081;
        int batchIntervalMs = 100;
    };

    explicit WebSocketServer(QObject *parent = nullptr);
    ~WebSocketServer() override;

    bool start(const Config &config);
    void stop();

    bool isListening() const;
    int serverPort() const;

    WebSocketHandler* handler() const;

signals:
    void started(const QString &host, int port);
    void failed(const QString &message);

private slots:
    void onNewConnection();

private:
    QWebSocketServer *m_server = nullptr;
    WebSocketHandler *m_handler = nullptr;
    Config m_config;
};

#endif // WEBSOCKETSERVER_H
