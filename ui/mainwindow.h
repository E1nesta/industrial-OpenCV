#pragma once

#include <QImage>
#include <QList>
#include <QMainWindow>
#include <QRect>
#include <QString>
#include <QStringList>

#include "domain/entities/inspectionresult.h"
#include "domain/entities/deviceconfig.h"
#include "domain/entities/inputsource.h"
#include "domain/entities/inspectionrecord.h"
#include "domain/entities/recipe.h"
#include "common/logging/logevent.h"

class AppController;
class ImageViewWidget;

QT_BEGIN_NAMESPACE
namespace Ui
{
class MainWindow;
}
QT_END_NAMESPACE

// MainWindow 负责界面编排：
// 把用户操作转为控制器调用，并把控制器状态回填到各显示区域。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(AppController *controller, QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    // UI 交互入口：响应按钮点击和输入源切换。
    void onImportImageClicked();
    void onStartInspectionClicked();
    void onStopInspectionClicked();
    void onExportRecordsClicked();
    void onRecentRecordActivated(int row, int column);
    void onLoadConfigClicked();
    void onSaveConfigClicked();
    void onResetConfigClicked();
    void onTcpConnectClicked();
    void onInputSourceTypeChanged();
    void onBrowseInputSourceClicked();
    void onOpenInputSourceClicked();
    void onCloseInputSourceClicked();
    void onStartPreviewClicked();
    void onStopPreviewClicked();
    void onStartContinuousInspectionClicked();
    void onStopContinuousInspectionClicked();
    // 控制器回调：把后台状态和结果同步回界面。
    void onInspectionStarted();
    void onInspectionFinished(const InspectionResult &result, const QImage &resultImage);
    void onInspectionFailed(const QString &errorMessage);
    void onInspectionCanceled();
    void onInspectionRunningChanged(bool isRunning);
    void onControllerStatusChanged(const QString &message);
    void onCaptureStatusChanged(const CaptureStatusSnapshot &status);
    void onPreviewFrameUpdated(const QImage &previewImage);
    // 界面同步：根据控制器当前状态刷新控件和展示内容。
    void syncFromController();
    void syncRecentRecords();
    void syncCaptureState();
    void syncTcpState();
    void onLogFilterChanged();
    void onUiLogGenerated(const LogEvent &event);

private:
    // 参数收集：从界面控件读取当前配置。
    Recipe collectRecipe() const;
    DeviceConfig collectDeviceConfig() const;
    InputSourceConfig collectInputSourceConfig() const;
    QRect collectRoi() const;
    InputSourceConfig displayInputSourceConfig() const;

    // 结果与记录展示。
    void displayRecordDetails(const InspectionRecord &record);
    void updateRecentRecordsTable(const QList<InspectionRecord> &records);

    // 输入源与 ROI 辅助刷新。
    void setRoiControls(const QRect &roi);
    void updateRoiSummary();
    void updateInputSourceUi();

    // 日志过滤与显示。
    void refreshLogView();
    bool logMatchesFilters(const LogEvent &event) const;
    void ensureLogModuleOption(const QString &module);
    void appendLog(const QString &message);

    // 初始化阶段：绑定信号并配置界面默认状态。
    void bindSignals();
    void setupImageViews();
    void setupUiState();

    // UI 对象与控制器入口。
    Ui::MainWindow *ui;
    AppController *m_controller;

    // 图片展示控件与当前图片上下文。
    ImageViewWidget *m_sourceImageView = nullptr;
    ImageViewWidget *m_resultImageView = nullptr;
    QString m_currentImagePath;

    // 最近记录与界面日志缓存。
    QList<InspectionRecord> m_recentRecords;
    QList<LogEvent> m_uiLogEvents;
    QStringList m_knownLogModules;

    // 预览首帧渲染标记，避免重复日志。
    bool m_previewFrameRendered = false;
    bool m_activeInspectionWasContinuous = false;
};
