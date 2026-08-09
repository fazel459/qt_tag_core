#include <QCoreApplication>
#include <QDebug>
#include <QHostAddress>
#include "qmqtt.h"
#include "app/CoreApplication.h"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    QCoreApplication::setApplicationName("tag-core");
    QCoreApplication::setApplicationVersion("0.1.0");

//    QMQTT::Client client(QHostAddress::LocalHost, 1883);

//        QObject::connect(&client, &QMQTT::Client::connected, [&client]()
//        {
//            qInfo() << "TEST: CONNECTED";
//            client.subscribe("sensors/temperature", 1);
//            qInfo() << "TEST: SUBSCRIBED";
//        });

//        QObject::connect(&client, &QMQTT::Client::received, [](const QMQTT::Message& message)
//        {
//            qInfo() << "TEST: MESSAGE RECEIVED:"
//                    << "topic=" << message.topic()
//                    << "payload=" << QString::fromUtf8(message.payload());
//        });

//        QObject::connect(&client, &QMQTT::Client::error, [](QMQTT::ClientError error)
//        {
//            qWarning() << "TEST: ERROR:" << error;
//        });

//        client.connectToHost();

    CoreApplication core;

    if (!core.initialize())
    {
        return -1;
    }

    return app.exec();
}
