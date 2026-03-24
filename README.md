# Qt + OpenCV 视觉检测上位机

本项目是一个基于 `C++ / Qt Widgets / OpenCV` 的桌面端视觉检测系统，当前实现已覆盖输入接入、预览取帧、图像检测、结果展示、记录留痕与 TCP 输出等主流程。

## 技术栈

- C++
- Qt Widgets
- OpenCV
- SQLite
- QSettings + INI
- QThread + worker object
- QTcpSocket
- CMake

## 快速构建与运行

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug
.\build\mingw-debug\VisionInspectionSystem.exe
```

## 文档入口

1. [系统总览](docs/01-系统总览.md)
2. [模块职责与边界](docs/02-模块职责与边界.md)
