// 基础设施视觉：inspectionworker.h 负责后台巡检执行任务。
// 本文件连接任务输入与算法执行，输出统一巡检结果。
#pragma once

#include <atomic>

#include <QObject>

#include "domain/entities/inspectionoutput.h"

class LogManager;

// InspectionWorker 在后台线程执行一次完整检测任务。
// 它负责算法调用、取消控制和结果分发，不负责 UI 或持久化。
class InspectionWorker : public QObject
{
    Q_OBJECT

public:
    explicit InspectionWorker(LogManager *logManager, QObject *parent = nullptr);

    // 控制层取消接口。
    void resetCancellation();
    void requestCancel();

signals:
    // 发给控制层的检测结果回调信号。
    void completed(const InspectionOutput &output);
    void failed(const QString &inspectionId, const QString &errorMessage);
    void canceled(const QString &inspectionId);

public slots:
    // 检测任务入口：处理单次 InspectionTask。
    void process(const InspectionTask &request);

private:
    // 日志与取消状态。
    LogManager *m_logManager = nullptr;
    std::atomic_bool m_cancelRequested = false;
};
