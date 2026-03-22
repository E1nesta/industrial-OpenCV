#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

#include "models/detectresult.h"
#include "models/deviceconfig.h"
#include "models/inputsource.h"
#include "models/inspectionrecord.h"
#include "models/visionparam.h"
#include "logger/logevent.h"

class AppController;
class ImageViewWidget;

QT_BEGIN_NAMESPACE
namespace Ui
{
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(AppController *controller, QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onImportImageClicked();
    void onStartDetectionClicked();
    void onStopDetectionClicked();
    void onExportRecordsClicked();
    void onRecentRecordActivated(int row, int column);
    void onLoadParamClicked();
    void onSaveParamClicked();
    void onResetParamClicked();
    void onTcpConnectClicked();
    void onInputSourceTypeChanged();
    void onBrowseInputSourceClicked();
    void onOpenInputSourceClicked();
    void onCloseInputSourceClicked();
    void onStartPreviewClicked();
    void onStopPreviewClicked();
    void onDetectionStarted();
    void onDetectionFinished(const DetectResult &result, const QImage &resultImage);
    void onDetectionFailed(const QString &errorMessage);
    void onDetectionCanceled();
    void onDetectionRunningChanged(bool isRunning);
    void onControllerStatusChanged(const QString &message);
    void onCaptureStatusChanged(const CaptureStatusSnapshot &status);
    void onPreviewFrameUpdated(const QImage &previewImage);
    void syncFromController();
    void syncRecentRecords();
    void syncCaptureState();
    void syncTcpState();
    void onLogFilterChanged();
    void onRuntimeLogLevelChanged(const QString &levelName);
    void onUiLogGenerated(const LogEvent &event);

private:
    VisionParam collectVisionParam() const;
    DeviceConfig collectDeviceConfig() const;
    InputSourceConfig collectInputSourceConfig() const;
    QRect collectRoi() const;
    void displayRecordDetails(const InspectionRecord &record);
    void updateRecentRecordsTable(const QList<InspectionRecord> &records);
    void setRoiControls(const QRect &roi);
    void updateRoiSummary();
    void updateInputSourceUi();
    void refreshLogView();
    bool logMatchesFilters(const LogEvent &event) const;
    void ensureLogModuleOption(const QString &module);
    void appendLog(const QString &message);
    void bindSignals();
    void setupImageViews();
    void setupUiState();

    Ui::MainWindow *ui;
    AppController *m_controller;
    ImageViewWidget *m_sourceImageView = nullptr;
    ImageViewWidget *m_resultImageView = nullptr;
    QString m_currentImagePath;
    QList<InspectionRecord> m_recentRecords;
    QList<LogEvent> m_uiLogEvents;
    QStringList m_knownLogModules;
    bool m_previewFrameRendered = false;
};
