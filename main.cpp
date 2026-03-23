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

    // 启动顺序：先构建控制器，再把控制器注入主窗口，最后统一初始化后台链路。
    AppController controller;
    MainWindow window(&controller);
    window.show();

    // initialize 会加载配置并启动各 worker 线程。
    controller.initialize();

    return app.exec();
}

