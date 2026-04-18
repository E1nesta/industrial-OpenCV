// 程序入口：main.cpp 负责应用初始化与主窗口启动。
// 本文件连接 Qt 运行时与应用层控制器初始化流程。
#include <QApplication>

#include "application/controllers/appcontroller.h"
#include "common/config/constants.h"
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

