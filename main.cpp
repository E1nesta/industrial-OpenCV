#include <QApplication>

#include "app/appcontroller.h"
#include "common/constants.h"
#include "ui/mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(constants::kApplicationName);
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setOrganizationName(constants::kOrganizationName);

    AppController controller;
    MainWindow window(&controller);
    window.show();

    controller.initialize();

    return app.exec();
}

