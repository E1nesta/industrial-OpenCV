#pragma once

#include <QMainWindow>
#include <QString>

#include "models/detectresult.h"
#include "models/deviceconfig.h"
#include "models/inspectionrecord.h"
#include "models/visionparam.h"

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
    void onDetectionStarted();
    void onDetectionFinished(const DetectResult &result, const QImage &resultImage);
    void onDetectionFailed(const QString &errorMessage);
    void onDetectionCanceled();
    void onDetectionRunningChanged(bool isRunning);
    void onControllerStatusChanged(const QString &message);
    void syncFromController();
    void syncRecentRecords();
    void syncTcpState();

private:
    VisionParam collectVisionParam() const;
    DeviceConfig collectDeviceConfig() const;
    void displayRecordDetails(const InspectionRecord &record);
    void updateRecentRecordsTable(const QList<InspectionRecord> &records);
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
};
