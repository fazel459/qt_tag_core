#include <QCoreApplication>

#include "app/CoreApplication.h"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    QCoreApplication::setApplicationName("tag-core");
    QCoreApplication::setApplicationVersion("0.1.0");

    CoreApplication core;

    if (!core.initialize())
    {
        return -1;
    }

    return app.exec();
}