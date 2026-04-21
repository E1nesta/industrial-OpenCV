// 表现层：mainwindow.cpp 负责界面交互与状态展示。
// 本文件位于巡检流程展示端，承接用户操作与结果回显。
#include "ui/mainwindow.h"

#include "ui_mainwindow.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QImage>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStatusBar>
#include <QTableWidgetItem>
#include <QTextDocument>
#include <QTextStream>
#include <QVBoxLayout>

#include "application/controllers/appcontroller.h"
#include "ui/historyreviewhelper.h"
#include "ui/imageviewwidget.h"

namespace
{
constexpr int kUiLogHistoryLimit = 500;

QString escapeCsvField(const QString &value)
{
    QString escaped = value;
    escaped.replace('"', QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

QString inputSourceTypeLabel(InputSourceType type)
{
    switch (type) {
    case InputSourceType::VideoFile:
        return QStringLiteral("视频文件");
    case InputSourceType::Camera:
        return QStringLiteral("摄像头");
    case InputSourceType::FileImage:
    default:
        return QStringLiteral("本地图片");
    }
}

QString sourcePlaceholderText(InputSourceType type)
{
    switch (type) {
    case InputSourceType::VideoFile:
        return QStringLiteral("等待视频预览帧");
    case InputSourceType::Camera:
        return QStringLiteral("等待摄像头预览帧");
    case InputSourceType::FileImage:
    default:
        return QStringLiteral("等待导入图片");
    }
}

QString inputSourcePathLabelText(InputSourceType type)
{
    switch (type) {
    case InputSourceType::VideoFile:
        return QStringLiteral("视频");
    case InputSourceType::FileImage:
        return QStringLiteral("图片");
    case InputSourceType::Camera:
    default:
        return QStringLiteral("路径");
    }
}

QString inputSourcePathPlaceholderText(InputSourceType type)
{
    switch (type) {
    case InputSourceType::VideoFile:
        return QStringLiteral("选择视频文件");
    case InputSourceType::FileImage:
        return QStringLiteral("选择图片文件");
    case InputSourceType::Camera:
    default:
        return QStringLiteral("");
    }
}

QString inspectionResultText(bool isOk)
{
    return isOk ? QStringLiteral("OK") : QStringLiteral("NG");
}

QString roiSummaryText(const QRect &roi)
{
    if (!roi.isValid() || roi.isEmpty()) {
        return QStringLiteral("未设置");
    }

    return QStringLiteral("x=%1, y=%2, w=%3, h=%4")
        .arg(roi.x())
        .arg(roi.y())
        .arg(roi.width())
        .arg(roi.height());
}
} // namespace

MainWindow::MainWindow(AppController *controller, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_controller(controller)
{
    ui->setupUi(this);
    setupImageViews();
    setupUiState();
    bindSignals();
    syncFromController();
    syncRecentRecords();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onImportImageClicked()
{
    // 输入选择入口：文件模式导入图片，视频模式选择视频源并等待预览。
    const InputSourceConfig config = collectInputSourceConfig();
    const bool isVideoMode = config.type == InputSourceType::VideoFile;
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        isVideoMode ? tr("选择视频文件") : tr("选择检测图片"),
        ui->inputSourcePathLineEdit->text().trimmed(),
        isVideoMode ? tr("视频文件 (*.mp4 *.avi *.mov *.mkv)")
                    : tr("图片文件 (*.png *.jpg *.jpeg *.bmp)"));

    if (filePath.isEmpty()) {
        return;
    }

    if (isVideoMode) {
        ui->inputSourcePathLineEdit->setText(filePath);
        m_controller->setInputSourceConfig(collectInputSourceConfig());
        // 视频模式只更新来源与占位状态，实际画面由预览帧驱动刷新。
        m_currentImagePath.clear();
        m_previewFrameRendered = false;
        m_sourceImageView->clearImage();
        m_resultImageView->clearImage();
        ui->currentImageValueLabel->setText(filePath);
        ui->resultStateValueLabel->setText(QStringLiteral("待预览"));
        ui->defectCountValueLabel->setText(QStringLiteral("--"));
        ui->processTimeValueLabel->setText(QStringLiteral("--"));
        ui->summaryTextValueLabel->setText(QStringLiteral("等待预览帧。"));
        m_controller->logManager().info(QStringLiteral("界面"), QStringLiteral("已选择视频文件：%1").arg(filePath));
        statusBar()->showMessage(tr("视频文件已选择"), 3000);
        syncCaptureState();
        return;
    }

    const QImage image(filePath);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("图片加载失败"), tr("所选文件无法作为图片加载。"));
        m_controller->logManager().warn(QStringLiteral("界面"), QStringLiteral("图片加载失败：%1").arg(filePath));
        return;
    }

    ui->inputSourcePathLineEdit->setText(filePath);
    m_controller->setInputSourceConfig(collectInputSourceConfig());
    m_currentImagePath = filePath;
    m_sourceImageView->setImage(image);
    m_resultImageView->clearImage();
    ui->currentImageValueLabel->setText(filePath);
    ui->resultStateValueLabel->setText(QStringLiteral("就绪"));
    ui->defectCountValueLabel->setText(QStringLiteral("--"));
    ui->processTimeValueLabel->setText(QStringLiteral("--"));
    ui->summaryTextValueLabel->setText(QStringLiteral("已载入待检测图片，等待开始检测。"));

    m_controller->logManager().info(QStringLiteral("界面"), QStringLiteral("已导入图片：%1").arg(filePath));
    statusBar()->showMessage(tr("图片已加载"), 3000);
}

void MainWindow::onStartInspectionClicked()
{
    // 检测入口统一先同步最新参数，再按输入源类型分发到图片/当前帧检测链。
    m_controller->setRecipe(collectRecipe());

    if (collectInputSourceConfig().type == InputSourceType::FileImage) {
        const QString candidatePath = ui->inputSourcePathLineEdit->text().trimmed();
        const bool shouldLoadCandidate = !candidatePath.isEmpty() && candidatePath != m_currentImagePath;
        const bool hasEffectivePath = !candidatePath.isEmpty() || !m_currentImagePath.isEmpty();
        if (!hasEffectivePath) {
            QMessageBox::information(this, tr("尚未选择图片"), tr("请先导入一张待检测图片。"));
            return;
        }

        if (m_currentImagePath.isEmpty() || shouldLoadCandidate) {
            const QString pathToLoad = candidatePath.isEmpty() ? m_currentImagePath : candidatePath;
            const QImage image(pathToLoad);
            if (image.isNull()) {
                QMessageBox::warning(this, tr("图片加载失败"), tr("当前路径无法作为图片加载，请重新选择图片。"));
                return;
            }

            m_currentImagePath = pathToLoad;
            m_sourceImageView->setImage(image);
            ui->currentImageValueLabel->setText(pathToLoad);
        }

        if (!m_controller->startInspection(m_currentImagePath)) {
            if (!m_controller->isInspectionRunning()) {
                QMessageBox::warning(this, tr("检测未启动"), m_controller->statusMessage());
            }
            return;
        }
        return;
    }

    if (!m_controller->inspectCurrentFrame()) {
        if (!m_controller->isInspectionRunning()) {
            QMessageBox::warning(this, tr("检测未启动"), m_controller->statusMessage());
        }
        return;
    }
}

void MainWindow::onStopInspectionClicked()
{
    // UI 停止入口：请求控制器取消当前检测任务并更新界面提示。
    if (!m_controller->cancelInspection()) {
        return;
    }

    ui->resultStateValueLabel->setText(QStringLiteral("取消中"));
    ui->summaryTextValueLabel->setText(QStringLiteral("正在取消当前检测任务。"));
    statusBar()->showMessage(tr("正在取消检测任务"), 3000);
}

void MainWindow::onExportRecordsClicked()
{
    // 记录导出入口：把最近记录导出为 CSV 供演示归档。
    if (m_recentRecords.isEmpty()) {
        QMessageBox::information(this, tr("暂无可导出记录"), tr("当前没有可导出的最近记录。"));
        return;
    }

    const QString suggestedFileName =
        QStringLiteral("inspection_records_%1.csv")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("导出最近记录"),
        suggestedFileName,
        tr("CSV 文件 (*.csv)"));

    if (filePath.isEmpty()) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("导出失败"), tr("无法写入目标文件。"));
        m_controller->logManager().warn(
            QStringLiteral("界面"),
            QStringLiteral("导出最近记录失败：%1").arg(filePath));
        return;
    }

    QTextStream stream(&file);
    stream << "timestamp,batch_no,recipe_name,result,defect_count,process_time_ms,summary_text,image_path,result_image_path\n";

    for (const InspectionRecord &record : m_recentRecords) {
        stream << escapeCsvField(record.timestamp) << ','
               << escapeCsvField(record.batchNo) << ','
               << escapeCsvField(record.recipeName) << ','
               << escapeCsvField(inspectionResultText(record.isOk)) << ','
               << record.defectCount << ','
               << QString::number(record.processTimeMs, 'f', 2) << ','
               << escapeCsvField(record.summaryText) << ','
               << escapeCsvField(record.imagePath) << ','
               << escapeCsvField(record.resultImagePath) << '\n';
    }

    file.close();
    m_controller->logManager().info(
        QStringLiteral("界面"),
        QStringLiteral("最近记录已导出：%1").arg(filePath));
    statusBar()->showMessage(tr("最近记录导出完成"), 3000);
}

void MainWindow::onRecentRecordActivated(int row, int column)
{
    // 历史记录入口：双击行后加载对应原图和结果图。
    Q_UNUSED(column);

    const CaptureStatusSnapshot &captureStatus = m_controller->captureStatus();
    const bool captureBusy = captureStatus.opened
        || captureStatus.state == CaptureState::Opening
        || captureStatus.state == CaptureState::Closing;
    if (m_controller->isInspectionRunning() || m_controller->isContinuousInspectionEnabled()) {
        statusBar()->showMessage(tr("检测运行中，暂不支持切换到历史记录"), 3000);
        return;
    }
    if (captureBusy) {
        statusBar()->showMessage(tr("输入源占用中，请先关闭输入源后再回看历史记录"), 3000);
        return;
    }

    if (row < 0 || row >= m_recentRecords.size()) {
        return;
    }

    displayRecordDetails(m_recentRecords.at(row));
}

void MainWindow::onLoadConfigClicked()
{
    // 参数重载入口：从配置文件回填控制器与界面状态。
    m_controller->reloadConfig();
}

void MainWindow::onSaveConfigClicked()
{
    // 参数保存入口：采集当前界面参数并统一落盘。
    m_controller->setRecipe(collectRecipe());
    m_controller->setDeviceConfig(collectDeviceConfig());
    m_controller->setInputSourceConfig(collectInputSourceConfig());
    m_controller->saveCurrentParam();
}

void MainWindow::onResetConfigClicked()
{
    // 参数重置入口：恢复默认值并触发界面同步。
    m_controller->resetToDefaults();
}

void MainWindow::onTcpConnectClicked()
{
    // TCP 按钮入口：按当前连接态在“连接/断开”之间切换。
    m_controller->setDeviceConfig(collectDeviceConfig());

    if (m_controller->isTcpConnected()) {
        m_controller->disconnectTcpDevice();
        return;
    }

    if (!m_controller->connectTcpDevice()) {
        QMessageBox::warning(this, tr("TCP 连接失败"), m_controller->tcpStatusText());
    }
}

void MainWindow::onInputSourceTypeChanged()
{
    // 输入源类型切换时，先处理打开态收尾，再重置界面显示上下文。
    const InputSourceConfig previousConfig = m_controller->inputSourceConfig();
    const InputSourceType nextType =
        static_cast<InputSourceType>(ui->inputSourceTypeComboBox->currentIndex());
    if (previousConfig.type != nextType && m_controller->captureStatus().opened) {
        m_controller->closeInputSource();
    }

    if (previousConfig.type != nextType) {
        m_currentImagePath.clear();
        m_previewFrameRendered = false;
        m_sourceImageView->clearImage();
        m_resultImageView->clearImage();
        ui->inputSourcePathLineEdit->clear();
        if (nextType == InputSourceType::FileImage) {
            ui->currentImageValueLabel->setText(QStringLiteral("未选择"));
        } else {
            ui->currentImageValueLabel->setText(sourcePlaceholderText(nextType));
        }
        ui->resultStateValueLabel->setText(QStringLiteral("--"));
        ui->defectCountValueLabel->setText(QStringLiteral("--"));
        ui->processTimeValueLabel->setText(QStringLiteral("--"));
        ui->summaryTextValueLabel->setText(QStringLiteral("--"));
    }

    const InputSourceConfig config = collectInputSourceConfig();
    m_controller->setInputSourceConfig(config);
    updateInputSourceUi();
    syncCaptureState();
}

void MainWindow::onBrowseInputSourceClicked()
{
    // 浏览入口复用导入流程，保持来源选择路径一致。
    onImportImageClicked();
}

void MainWindow::onOpenInputSourceClicked()
{
    // 打开输入源入口：提交当前来源配置并触发控制器打开流程。
    m_controller->setInputSourceConfig(collectInputSourceConfig());
    if (!m_controller->openInputSource()) {
        QMessageBox::warning(this, tr("输入源打开失败"), m_controller->statusMessage());
    }
}

void MainWindow::onCloseInputSourceClicked()
{
    // 关闭输入源入口：统一交给控制器收敛采集状态。
    m_controller->closeInputSource();
}

void MainWindow::onStartPreviewClicked()
{
    // 启动预览入口：提交来源配置后触发预览链路。
    m_controller->setInputSourceConfig(collectInputSourceConfig());
    if (!m_controller->startPreview()) {
        QMessageBox::warning(this, tr("预览未启动"), m_controller->statusMessage());
    }
}

void MainWindow::onStopPreviewClicked()
{
    // 停止预览入口：终止预览链路并保持输入源连接态。
    m_controller->stopPreview();
}

void MainWindow::onStartContinuousInspectionClicked()
{
    // 连续检测入口：同步参数后启动“预览帧驱动”的检测节拍。
    m_controller->setRecipe(collectRecipe());
    m_controller->setContinuousInspectionIntervalMs(ui->continuousInspectionIntervalSpinBox->value());
    m_controller->setInputSourceConfig(collectInputSourceConfig());

    if (!m_controller->startContinuousInspection()) {
        QMessageBox::warning(this, tr("连续检测未启动"), m_controller->statusMessage());
        return;
    }

    statusBar()->showMessage(tr("连续检测已启动"), 3000);
}

void MainWindow::onStopContinuousInspectionClicked()
{
    // 连续检测停止入口：关闭节拍并刷新界面提示。
    m_controller->stopContinuousInspection();
    statusBar()->showMessage(tr("连续检测已停止"), 3000);
}

void MainWindow::onInspectionStarted()
{
    // 连续检测保留上一帧结果，避免结果区在每轮开始时先空一下造成跳闪。
    m_activeInspectionWasContinuous = m_controller->isContinuousInspectionEnabled();
    if (!m_activeInspectionWasContinuous) {
        m_resultImageView->clearImage();
    }
    ui->resultStateValueLabel->setText(QStringLiteral("检测中"));
    ui->defectCountValueLabel->setText(QStringLiteral("--"));
    ui->processTimeValueLabel->setText(QStringLiteral("--"));
    ui->summaryTextValueLabel->setText(QStringLiteral("检测任务执行中。"));
    statusBar()->showMessage(tr("检测任务执行中"), 3000);
}

void MainWindow::onInspectionFinished(const InspectionResult &result, const QImage &resultImage)
{
    // 检测完成回调：展示结果图并更新检测摘要信息。
    m_controller->logManager().info(
        QStringLiteral("界面"),
        QStringLiteral("界面收到检测完成：inspectionId=%1 isNull=%2 size=%3x%4")
            .arg(result.inspectionId)
            .arg(resultImage.isNull() ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(resultImage.width())
            .arg(resultImage.height()),
        false);
    m_resultImageView->setImage(resultImage);
    m_controller->logManager().info(
        QStringLiteral("界面"),
        QStringLiteral("结果图已显示到界面：inspectionId=%1").arg(result.inspectionId),
        false);
    ui->resultStateValueLabel->setText(inspectionResultText(result.isOk));
    ui->defectCountValueLabel->setText(QString::number(result.defectCount));
    ui->processTimeValueLabel->setText(QStringLiteral("%1 ms").arg(result.elapsedMs, 0, 'f', 2));
    ui->summaryTextValueLabel->setText(
        result.summaryText.isEmpty() ? QStringLiteral("--") : result.summaryText);
    m_activeInspectionWasContinuous = false;
    statusBar()->showMessage(tr("检测完成"), 3000);
}

void MainWindow::onInspectionFailed(const QString &errorMessage)
{
    // 检测失败回调：更新失败态并弹出错误说明。
    ui->resultStateValueLabel->setText(QStringLiteral("失败"));
    ui->defectCountValueLabel->setText(QStringLiteral("--"));
    ui->processTimeValueLabel->setText(QStringLiteral("--"));
    ui->summaryTextValueLabel->setText(errorMessage);
    m_resultImageView->clearImage();
    statusBar()->showMessage(tr("检测失败"), 3000);
    if (!m_activeInspectionWasContinuous) {
        QMessageBox::warning(this, tr("检测失败"), errorMessage);
    }
    m_activeInspectionWasContinuous = false;
}

void MainWindow::onInspectionCanceled()
{
    // 检测取消回调：复位结果展示并提示任务已取消。
    ui->resultStateValueLabel->setText(QStringLiteral("已取消"));
    ui->defectCountValueLabel->setText(QStringLiteral("--"));
    ui->processTimeValueLabel->setText(QStringLiteral("--"));
    ui->summaryTextValueLabel->setText(QStringLiteral("检测已取消。"));
    m_resultImageView->clearImage();
    m_activeInspectionWasContinuous = false;
    statusBar()->showMessage(tr("检测任务已取消"), 3000);
}

void MainWindow::onInspectionRunningChanged(bool isRunning)
{
    // 检测运行态是界面启停规则的总开关：统一控制参数编辑、TCP 配置和采集操作。
    const bool cancelRequested = m_controller->isInspectionCancelRequested();
    const bool continuousEnabled = m_controller->isContinuousInspectionEnabled();
    const bool lockForActiveDetection = isRunning || continuousEnabled;

    if (isRunning && cancelRequested) {
        ui->resultStateValueLabel->setText(QStringLiteral("取消中"));
    } else if (isRunning) {
        ui->resultStateValueLabel->setText(QStringLiteral("检测中"));
    }

    ui->startInspectionButton->setEnabled(!lockForActiveDetection);
    ui->stopInspectionButton->setEnabled(isRunning && !cancelRequested);
    ui->stopInspectionButton->setText(cancelRequested ? QStringLiteral("取消中...") : QStringLiteral("停止检测"));
    ui->loadConfigButton->setEnabled(!lockForActiveDetection);
    ui->saveConfigButton->setEnabled(!lockForActiveDetection);
    ui->resetConfigButton->setEnabled(!lockForActiveDetection);
    ui->tcpConnectButton->setEnabled(!lockForActiveDetection);
    ui->recipeNameLineEdit->setEnabled(!lockForActiveDetection);
    ui->enableDefectDetectionCheckBox->setEnabled(!lockForActiveDetection);
    ui->thresholdSpinBox->setEnabled(!lockForActiveDetection);
    ui->minAreaSpinBox->setEnabled(!lockForActiveDetection);
    ui->maxAreaSpinBox->setEnabled(!lockForActiveDetection);
    ui->morphologyCheckBox->setEnabled(!lockForActiveDetection);
    ui->saveSourceImageCheckBox->setEnabled(!lockForActiveDetection);
    ui->saveResultImageCheckBox->setEnabled(!lockForActiveDetection);
    ui->enableTcpResultCheckBox->setEnabled(!lockForActiveDetection);
    ui->roiXSpinBox->setEnabled(!lockForActiveDetection);
    ui->roiYSpinBox->setEnabled(!lockForActiveDetection);
    ui->roiWidthSpinBox->setEnabled(!lockForActiveDetection);
    ui->roiHeightSpinBox->setEnabled(!lockForActiveDetection);
    ui->clearRoiButton->setEnabled(!lockForActiveDetection);
    ui->imageSavePathLineEdit->setEnabled(!lockForActiveDetection);
    ui->browseImageSavePathButton->setEnabled(!lockForActiveDetection);
    syncCaptureState();
    syncTcpState();
}

void MainWindow::onControllerStatusChanged(const QString &message)
{
    // 控制器状态回调：统一同步到状态栏和状态标签。
    ui->statusValueLabel->setText(message);
    statusBar()->showMessage(message, 5000);
}

void MainWindow::onCaptureStatusChanged(const CaptureStatusSnapshot &status)
{
    // 采集状态回调：界面不直接消费细节，仅触发统一按钮状态同步。
    Q_UNUSED(status);
    syncCaptureState();
}

void MainWindow::onPreviewFrameUpdated(const QImage &previewImage)
{
    // 预览帧是采集链到界面的主通道，首帧到达时额外记录一次提示日志。
    const CaptureStatusSnapshot &status = m_controller->captureStatus();
    if (previewImage.isNull()) {
        m_previewFrameRendered = false;
        if (status.source.type != InputSourceType::FileImage) {
            m_sourceImageView->clearImage();
        }
        syncCaptureState();
        return;
    }

    m_sourceImageView->setImage(previewImage);
    if (!m_previewFrameRendered) {
        m_previewFrameRendered = true;
        m_controller->logManager().info(
            QStringLiteral("采集"),
            QStringLiteral("首帧预览已显示：source=%1 frameIndex=%2 size=%3x%4")
                .arg(status.source.sourceName.isEmpty() ? inputSourceTypeLabel(status.source.type)
                                                        : status.source.sourceName)
                .arg(status.lastFrameIndex)
                .arg(previewImage.width())
                .arg(previewImage.height()),
            false);
    }
    const QString sourceName = status.source.sourceName.isEmpty() ? inputSourceTypeLabel(status.source.type)
                                                                  : status.source.sourceName;
    ui->currentImageValueLabel->setText(
        status.lastFrameIndex >= 0 ? QStringLiteral("%1 / frame=%2").arg(sourceName).arg(status.lastFrameIndex)
                                   : sourceName);
    syncCaptureState();
}

void MainWindow::syncFromController()
{
    // 全量同步入口：把控制器当前配置与状态回填到所有相关控件。
    const Recipe &param = m_controller->recipe();
    const DeviceConfig &deviceConfig = m_controller->deviceConfig();
    const InputSourceConfig &inputConfig = m_controller->inputSourceConfig();
    const InputSourceConfig displayedInputConfig = displayInputSourceConfig();
    const QString configuredSourcePath = inputConfig.sourcePath.trimmed();
    const bool loadedFileOutOfSync =
        !m_currentImagePath.isEmpty()
        && (inputConfig.type != InputSourceType::FileImage || configuredSourcePath != m_currentImagePath);

    ui->thresholdSpinBox->setValue(param.threshold);
    ui->minAreaSpinBox->setValue(param.minArea);
    ui->maxAreaSpinBox->setValue(param.maxArea);
    ui->recipeNameLineEdit->setText(param.recipeName);
    ui->enableDefectDetectionCheckBox->setChecked(param.enableDefectDetection);
    ui->activeRecipeLabel->setText(QStringLiteral("当前配方"));
    ui->morphologyCheckBox->setChecked(param.enableMorphology);
    ui->saveSourceImageCheckBox->setChecked(param.saveSourceImage);
    ui->saveResultImageCheckBox->setChecked(param.saveResultImage);
    ui->enableTcpResultCheckBox->setChecked(param.enableTcpResult);
    setRoiControls(param.roi);
    ui->imageSavePathLineEdit->setText(param.imageSavePath);
    ui->activeRecipeValueLabel->setText(param.recipeName);
    ui->tcpIpLineEdit->setText(deviceConfig.ip);
    ui->tcpPortSpinBox->setValue(deviceConfig.port);
    {
        const QSignalBlocker typeBlocker(ui->inputSourceTypeComboBox);
        ui->inputSourceTypeComboBox->setCurrentIndex(static_cast<int>(displayedInputConfig.type));
    }
    ui->inputSourcePathLineEdit->setText(displayedInputConfig.sourcePath.trimmed());
    ui->cameraDeviceSpinBox->setValue(displayedInputConfig.deviceIndex);
    ui->previewIntervalSpinBox->setValue(displayedInputConfig.previewIntervalMs);
    ui->continuousInspectionIntervalSpinBox->setValue(m_controller->continuousInspectionIntervalMs());
    ui->stageValueLabel->setText(m_controller->projectStage());
    ui->configPathValueLabel->setText(m_controller->configFilePath());

    if (!m_controller->statusMessage().isEmpty()) {
        ui->statusValueLabel->setText(m_controller->statusMessage());
    }

    if (loadedFileOutOfSync) {
        m_currentImagePath.clear();
        m_sourceImageView->clearImage();
        m_resultImageView->clearImage();
        ui->resultStateValueLabel->setText(QStringLiteral("--"));
        ui->defectCountValueLabel->setText(QStringLiteral("--"));
        ui->processTimeValueLabel->setText(QStringLiteral("--"));
        ui->summaryTextValueLabel->setText(QStringLiteral("--"));
    }

    updateInputSourceUi();
    onInspectionRunningChanged(m_controller->isInspectionRunning());
    syncCaptureState();
    syncTcpState();
}

void MainWindow::syncRecentRecords()
{
    // 最近记录同步入口：从控制器拉取数据并刷新表格。
    updateRecentRecordsTable(m_controller->recentRecords());
}

Recipe MainWindow::collectRecipe() const
{
    Recipe param = m_controller->recipe();
    const QString recipeName = ui->recipeNameLineEdit->text().trimmed();
    param.recipeName = recipeName.isEmpty() ? Recipe{}.recipeName : recipeName;
    param.enableDefectDetection = ui->enableDefectDetectionCheckBox->isChecked();
    param.threshold = ui->thresholdSpinBox->value();
    param.minArea = ui->minAreaSpinBox->value();
    param.maxArea = ui->maxAreaSpinBox->value();
    param.enableMorphology = ui->morphologyCheckBox->isChecked();
    param.saveSourceImage = ui->saveSourceImageCheckBox->isChecked();
    param.saveResultImage = ui->saveResultImageCheckBox->isChecked();
    param.enableTcpResult = ui->enableTcpResultCheckBox->isChecked();
    param.roi = collectRoi();
    const QString imageSavePath = ui->imageSavePathLineEdit->text().trimmed();
    param.imageSavePath = imageSavePath.isEmpty() ? Recipe{}.imageSavePath : imageSavePath;
    return param;
}

DeviceConfig MainWindow::collectDeviceConfig() const
{
    DeviceConfig config = m_controller->deviceConfig();
    config.ip = ui->tcpIpLineEdit->text().trimmed();
    config.port = ui->tcpPortSpinBox->value();
    return config;
}

InputSourceConfig MainWindow::collectInputSourceConfig() const
{
    InputSourceConfig config = m_controller->inputSourceConfig();
    config.type = static_cast<InputSourceType>(ui->inputSourceTypeComboBox->currentIndex());
    config.sourcePath = ui->inputSourcePathLineEdit->text().trimmed();
    config.deviceIndex = ui->cameraDeviceSpinBox->value();
    config.previewIntervalMs = ui->previewIntervalSpinBox->value();

    switch (config.type) {
    case InputSourceType::Camera:
        config.sourceName = QStringLiteral("camera-%1").arg(config.deviceIndex);
        break;
    case InputSourceType::VideoFile:
    case InputSourceType::FileImage:
    default:
        config.sourceName = QFileInfo(config.sourcePath).fileName();
        break;
    }

    return config;
}

InputSourceConfig MainWindow::displayInputSourceConfig() const
{
    const CaptureStatusSnapshot &status = m_controller->captureStatus();
    if (status.opened
        || status.state == CaptureState::Opening
        || status.state == CaptureState::Closing) {
        return status.source;
    }

    return m_controller->inputSourceConfig();
}

QRect MainWindow::collectRoi() const
{
    const int width = ui->roiWidthSpinBox->value();
    const int height = ui->roiHeightSpinBox->value();
    if (width <= 0 || height <= 0) {
        return {};
    }

    return QRect(ui->roiXSpinBox->value(), ui->roiYSpinBox->value(), width, height);
}

void MainWindow::displayRecordDetails(const InspectionRecord &record)
{
    // 记录详情展示：加载历史原图/结果图并更新结果摘要区域。
    const HistoryReviewContent reviewContent = loadHistoryReviewContent(record);
    for (const QString &warning : reviewContent.warnings) {
        m_controller->logManager().warn(QStringLiteral("界面"), warning);
    }

    if (!reviewContent.hasDisplayableImage()) {
        statusBar()->showMessage(reviewContent.statusMessage, 3000);
        return;
    }

    InputSourceConfig inputConfig = m_controller->inputSourceConfig();
    inputConfig.type = InputSourceType::FileImage;
    inputConfig.sourcePath =
        reviewContent.canReuseAsInspectionInput() ? reviewContent.inspectionInputPath : QString();
    inputConfig.sourceName =
        reviewContent.canReuseAsInspectionInput() ? reviewContent.inspectionInputName : QString();
    {
        const QSignalBlocker typeBlocker(ui->inputSourceTypeComboBox);
        ui->inputSourceTypeComboBox->setCurrentIndex(static_cast<int>(InputSourceType::FileImage));
    }
    m_controller->setInputSourceConfig(inputConfig);
    ui->inputSourcePathLineEdit->setText(inputConfig.sourcePath);
    updateInputSourceUi();

    ui->resultStateValueLabel->setText(inspectionResultText(record.isOk));
    ui->defectCountValueLabel->setText(QString::number(record.defectCount));
    ui->processTimeValueLabel->setText(QStringLiteral("%1 ms").arg(record.processTimeMs, 0, 'f', 2));
    ui->activeRecipeLabel->setText(QStringLiteral("记录配方"));
    ui->activeRecipeValueLabel->setText(record.recipeName.isEmpty() ? QStringLiteral("--") : record.recipeName);
    ui->summaryTextValueLabel->setText(record.summaryText.isEmpty() ? QStringLiteral("--") : record.summaryText);

    m_sourceImageView->clearImage();
    m_resultImageView->clearImage();

    if (reviewContent.sourceImageReady) {
        m_currentImagePath = reviewContent.inspectionInputPath;
        m_sourceImageView->setImage(reviewContent.sourceImage);
    } else {
        m_currentImagePath.clear();
    }
    ui->currentImageValueLabel->setText(reviewContent.currentImageLabel);

    if (reviewContent.resultImageReady) {
        m_resultImageView->setImage(reviewContent.resultImage);
    }

    m_controller->logManager().info(
        QStringLiteral("界面"),
        QStringLiteral("已切换到历史记录：source=%1 result=%2")
            .arg(record.imagePath, record.resultImagePath));
    statusBar()->showMessage(reviewContent.statusMessage, 3000);
    syncCaptureState();
}

void MainWindow::refreshLogView()
{
    // 日志视图刷新：按过滤条件重建当前可见日志窗口。
    ui->logPlainTextEdit->clear();

    for (const LogEvent &event : m_uiLogEvents) {
        if (logMatchesFilters(event)) {
            appendLog(event.formattedLine);
        }
    }
}

bool MainWindow::logMatchesFilters(const LogEvent &event) const
{
    // 日志过滤判定：级别与模块双条件同时生效。
    const QString levelFilter = ui->logLevelFilterComboBox->currentText();
    if (levelFilter != QStringLiteral("全部") && event.level != levelFilter) {
        return false;
    }

    const QString moduleFilter = ui->logModuleFilterComboBox->currentText();
    if (moduleFilter != QStringLiteral("全部") && event.module != moduleFilter) {
        return false;
    }

    return true;
}

void MainWindow::ensureLogModuleOption(const QString &module)
{
    // 模块筛选维护：增量添加新模块，避免下拉项重复。
    if (module.isEmpty() || m_knownLogModules.contains(module)) {
        return;
    }

    m_knownLogModules.append(module);
    ui->logModuleFilterComboBox->addItem(module);
}

void MainWindow::appendLog(const QString &message)
{
    // 日志追加出口：保持日志控件写入路径单一。
    ui->logPlainTextEdit->appendPlainText(message);
}

void MainWindow::updateRecentRecordsTable(const QList<InspectionRecord> &records)
{
    // 表格刷新入口：把记录列表映射为可双击回看的表格行。
    m_recentRecords = records;
    ui->recentRecordsTableWidget->clearContents();
    ui->recentRecordsTableWidget->setRowCount(records.size());

    for (int row = 0; row < records.size(); ++row) {
        const InspectionRecord &record = records.at(row);
        const QString displayPath =
            !record.imagePath.isEmpty() ? record.imagePath : record.resultImagePath;
        auto *timestampItem = new QTableWidgetItem(record.timestamp);
        auto *resultItem = new QTableWidgetItem(inspectionResultText(record.isOk));
        auto *defectItem = new QTableWidgetItem(QString::number(record.defectCount));
        auto *timeItem = new QTableWidgetItem(QStringLiteral("%1").arg(record.processTimeMs, 0, 'f', 2));
        auto *imageItem = new QTableWidgetItem(QFileInfo(displayPath).fileName());
        const QString tooltip = QStringLiteral("双击回看\n%1").arg(displayPath);

        timestampItem->setToolTip(tooltip);
        resultItem->setToolTip(tooltip);
        defectItem->setToolTip(tooltip);
        timeItem->setToolTip(tooltip);
        imageItem->setToolTip(tooltip);

        ui->recentRecordsTableWidget->setItem(row, 0, timestampItem);
        ui->recentRecordsTableWidget->setItem(row, 1, resultItem);
        ui->recentRecordsTableWidget->setItem(row, 2, defectItem);
        ui->recentRecordsTableWidget->setItem(row, 3, timeItem);
        ui->recentRecordsTableWidget->setItem(row, 4, imageItem);
    }

}

void MainWindow::setRoiControls(const QRect &roi)
{
    // ROI 控件回填：通过信号阻断避免回填触发二次联动。
    const QSignalBlocker xBlocker(ui->roiXSpinBox);
    const QSignalBlocker yBlocker(ui->roiYSpinBox);
    const QSignalBlocker widthBlocker(ui->roiWidthSpinBox);
    const QSignalBlocker heightBlocker(ui->roiHeightSpinBox);

    if (roi.isValid() && !roi.isEmpty()) {
        ui->roiXSpinBox->setValue(roi.x());
        ui->roiYSpinBox->setValue(roi.y());
        ui->roiWidthSpinBox->setValue(roi.width());
        ui->roiHeightSpinBox->setValue(roi.height());
    } else {
        ui->roiXSpinBox->setValue(0);
        ui->roiYSpinBox->setValue(0);
        ui->roiWidthSpinBox->setValue(0);
        ui->roiHeightSpinBox->setValue(0);
    }

    updateRoiSummary();
}

void MainWindow::updateRoiSummary()
{
    // ROI 摘要出口：把当前 ROI 统一格式化为可读文本。
    ui->roiValueLabel->setText(roiSummaryText(collectRoi()));
}

void MainWindow::updateInputSourceUi()
{
    // 输入源相关控件显隐统一在这里维护，避免散落在多个槽函数里。
    const InputSourceConfig config = displayInputSourceConfig();
    const bool isFileMode = config.type == InputSourceType::FileImage;
    const bool isCameraMode = config.type == InputSourceType::Camera;
    const bool isVideoMode = config.type == InputSourceType::VideoFile;

    ui->inputSourcePathLabel->setVisible(!isCameraMode);
    ui->inputSourcePathLineEdit->setVisible(!isCameraMode);
    ui->inputSourcePathLabel->setText(inputSourcePathLabelText(config.type));
    ui->inputSourcePathLineEdit->setPlaceholderText(inputSourcePathPlaceholderText(config.type));
    ui->browseInputSourceButton->setVisible(isVideoMode);
    ui->cameraDeviceLabel->setVisible(isCameraMode);
    ui->cameraDeviceSpinBox->setVisible(isCameraMode);
    ui->previewIntervalLabel->setVisible(!isFileMode);
    ui->previewIntervalSpinBox->setVisible(!isFileMode);
    ui->openInputSourceButton->setVisible(!isFileMode);
    ui->closeInputSourceButton->setVisible(!isFileMode);
    ui->startPreviewButton->setVisible(!isFileMode);
    ui->stopPreviewButton->setVisible(!isFileMode);
    ui->importImageButton->setVisible(isFileMode);
    m_sourceImageView->setPlaceholderText(sourcePlaceholderText(config.type));
    if (isFileMode && m_currentImagePath.isEmpty()) {
        ui->currentImageValueLabel->setText(QStringLiteral("未选择"));
    }

    ui->importImageButton->setText(
        config.type == InputSourceType::VideoFile ? QStringLiteral("选择视频文件")
                                                  : QStringLiteral("选择图片文件"));
    ui->startInspectionButton->setText(
        isFileMode ? QStringLiteral("开始检测") : QStringLiteral("检测当前帧"));
}

void MainWindow::bindSignals()
{
    // UI -> MainWindow：按钮、筛选和局部参数变化。
    connect(ui->importImageButton, &QPushButton::clicked, this, &MainWindow::onImportImageClicked);
    connect(
        ui->browseInputSourceButton,
        &QPushButton::clicked,
        this,
        &MainWindow::onBrowseInputSourceClicked);
    connect(
        ui->inputSourceTypeComboBox,
        &QComboBox::currentIndexChanged,
        this,
        [this](int) { onInputSourceTypeChanged(); });
    connect(ui->openInputSourceButton, &QPushButton::clicked, this, &MainWindow::onOpenInputSourceClicked);
    connect(ui->closeInputSourceButton, &QPushButton::clicked, this, &MainWindow::onCloseInputSourceClicked);
    connect(ui->startPreviewButton, &QPushButton::clicked, this, &MainWindow::onStartPreviewClicked);
    connect(ui->stopPreviewButton, &QPushButton::clicked, this, &MainWindow::onStopPreviewClicked);
    connect(
        ui->startContinuousInspectionButton,
        &QPushButton::clicked,
        this,
        &MainWindow::onStartContinuousInspectionClicked);
    connect(
        ui->stopContinuousInspectionButton,
        &QPushButton::clicked,
        this,
        &MainWindow::onStopContinuousInspectionClicked);
    connect(ui->startInspectionButton, &QPushButton::clicked, this, &MainWindow::onStartInspectionClicked);
    connect(ui->stopInspectionButton, &QPushButton::clicked, this, &MainWindow::onStopInspectionClicked);
    connect(ui->loadConfigButton, &QPushButton::clicked, this, &MainWindow::onLoadConfigClicked);
    connect(ui->saveConfigButton, &QPushButton::clicked, this, &MainWindow::onSaveConfigClicked);
    connect(ui->resetConfigButton, &QPushButton::clicked, this, &MainWindow::onResetConfigClicked);
    connect(ui->tcpConnectButton, &QPushButton::clicked, this, &MainWindow::onTcpConnectClicked);
    connect(ui->exportRecordsButton, &QPushButton::clicked, this, &MainWindow::onExportRecordsClicked);
    connect(
        ui->recentRecordsTableWidget,
        &QTableWidget::cellDoubleClicked,
        this,
        &MainWindow::onRecentRecordActivated);
    connect(ui->roiXSpinBox, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { updateRoiSummary(); });
    connect(ui->roiYSpinBox, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { updateRoiSummary(); });
    connect(ui->roiWidthSpinBox, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
        updateRoiSummary();
    });
    connect(ui->roiHeightSpinBox, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
        updateRoiSummary();
    });
    connect(ui->clearRoiButton, &QPushButton::clicked, this, [this]() {
        setRoiControls(QRect{});
        m_controller->logManager().info(QStringLiteral("界面"), QStringLiteral("已清空 ROI 参数。"));
    });
    connect(ui->browseImageSavePathButton, &QPushButton::clicked, this, [this]() {
        const QString currentPath = ui->imageSavePathLineEdit->text().trimmed();
        const QString directory = QFileDialog::getExistingDirectory(
            this,
            tr("选择图片保存目录"),
            currentPath.isEmpty() ? QString() : currentPath);
        if (directory.isEmpty()) {
            return;
        }

        ui->imageSavePathLineEdit->setText(directory);
        m_controller->logManager().info(
            QStringLiteral("界面"),
            QStringLiteral("图片保存目录已更新：%1").arg(directory));
    });
    connect(
        ui->logLevelFilterComboBox,
        &QComboBox::currentTextChanged,
        this,
        [this](const QString &) { onLogFilterChanged(); });
    connect(
        ui->logModuleFilterComboBox,
        &QComboBox::currentTextChanged,
        this,
        [this](const QString &) { onLogFilterChanged(); });
    connect(ui->clearLogButton, &QPushButton::clicked, this, [this]() {
        m_uiLogEvents.clear();
        refreshLogView();
    });

    // LogManager -> MainWindow：界面日志流。
    connect(&m_controller->logManager(), &LogManager::uiLogGenerated, this, &MainWindow::onUiLogGenerated);

    // AppController -> MainWindow：检测、采集、配置与通信状态回调。
    connect(m_controller, &AppController::inspectionStarted, this, &MainWindow::onInspectionStarted);
    connect(m_controller, &AppController::inspectionFinished, this, &MainWindow::onInspectionFinished);
    connect(m_controller, &AppController::inspectionFailed, this, &MainWindow::onInspectionFailed);
    connect(m_controller, &AppController::inspectionCanceled, this, &MainWindow::onInspectionCanceled);
    connect(
        m_controller,
        &AppController::inspectionRunningChanged,
        this,
        &MainWindow::onInspectionRunningChanged);
    connect(m_controller, &AppController::statusChanged, this, &MainWindow::onControllerStatusChanged);
    connect(m_controller, &AppController::recipeChanged, this, &MainWindow::syncFromController);
    connect(m_controller, &AppController::deviceConfigChanged, this, &MainWindow::syncFromController);
    connect(m_controller, &AppController::inputSourceConfigChanged, this, &MainWindow::syncFromController);
    connect(m_controller, &AppController::captureStatusChanged, this, &MainWindow::onCaptureStatusChanged);
    connect(m_controller, &AppController::previewFrameUpdated, this, &MainWindow::onPreviewFrameUpdated);
    connect(m_controller, &AppController::recordsChanged, this, &MainWindow::syncRecentRecords);
    connect(m_controller, &AppController::tcpStateChanged, this, &MainWindow::syncTcpState);
    connect(m_controller, &AppController::continuousInspectionStateChanged, this, [this](bool) {
        onInspectionRunningChanged(m_controller->isInspectionRunning());
        syncCaptureState();
    });
}

void MainWindow::setupImageViews()
{
    // 图像区域初始化：挂载源图和结果图自定义视图控件。
    auto *sourceLayout = new QVBoxLayout(ui->sourceImageHost);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    m_sourceImageView = new ImageViewWidget(ui->sourceImageHost);
    m_sourceImageView->setPlaceholderText(QStringLiteral("等待输入源图像"));
    sourceLayout->addWidget(m_sourceImageView);

    auto *resultLayout = new QVBoxLayout(ui->resultImageHost);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    m_resultImageView = new ImageViewWidget(ui->resultImageHost);
    m_resultImageView->setPlaceholderText(QStringLiteral("等待检测结果图"));
    resultLayout->addWidget(m_resultImageView);
}

void MainWindow::setupUiState()
{
    // 初始布局权重和控件默认值统一在这里设置，避免构造函数过重。
    ui->startPreviewButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui->stopPreviewButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui->workspaceLayout->setStretch(0, 3);
    ui->workspaceLayout->setStretch(1, 9);
    ui->workspaceLayout->setStretch(2, 4);
    ui->visualWorkspaceLayout->setStretch(0, 1);
    ui->visualWorkspaceLayout->setStretch(1, 1);
    ui->infoWorkspaceLayout->setStretch(0, 1);
    ui->infoWorkspaceLayout->setStretch(1, 1);
    ui->infoWorkspaceLayout->setStretch(2, 2);
    ui->currentImageLayout->setStretch(1, 1);
    ui->resultImageLayout->setStretch(1, 1);

    ui->logPlainTextEdit->document()->setMaximumBlockCount(200);
    ui->inputSourceTypeComboBox->addItems(
        QStringList{QStringLiteral("文件"),
                    QStringLiteral("视频模拟"),
                    QStringLiteral("摄像头")});
    ui->cameraDeviceSpinBox->setMinimum(0);
    ui->cameraDeviceSpinBox->setMaximum(16);
    ui->previewIntervalSpinBox->setMinimum(10);
    ui->previewIntervalSpinBox->setMaximum(1000);
    ui->previewIntervalSpinBox->setSuffix(QStringLiteral(" ms"));
    ui->continuousInspectionIntervalSpinBox->setMinimum(100);
    ui->continuousInspectionIntervalSpinBox->setMaximum(5000);
    ui->continuousInspectionIntervalSpinBox->setSingleStep(100);
    ui->continuousInspectionIntervalSpinBox->setSuffix(QStringLiteral(" ms"));
    ui->logLevelFilterComboBox->addItems(
        QStringList{QStringLiteral("全部"),
                    QStringLiteral("DEBUG"),
                    QStringLiteral("INFO"),
                    QStringLiteral("WARN"),
                    QStringLiteral("ERROR")});
    ui->logLevelFilterComboBox->setToolTip(QStringLiteral("仅过滤当前界面显示，不影响日志采集。"));
    ui->logModuleFilterComboBox->setToolTip(QStringLiteral("仅过滤当前界面显示，不影响日志采集。"));
    ui->logModuleFilterComboBox->addItem(QStringLiteral("全部"));
    m_knownLogModules = QStringList{QStringLiteral("全部")};
    ui->recentRecordsTableWidget->setColumnCount(5);
    ui->recentRecordsTableWidget->setHorizontalHeaderLabels(
        QStringList{QStringLiteral("时间"),
                    QStringLiteral("结果"),
                    QStringLiteral("缺陷数"),
                    QStringLiteral("耗时(ms)"),
                    QStringLiteral("图片")});
    ui->recentRecordsTableWidget->horizontalHeader()->setStretchLastSection(true);
    ui->recentRecordsTableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->recentRecordsTableWidget->verticalHeader()->setVisible(false);
    ui->recentRecordsTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->recentRecordsTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->recentRecordsTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->recentRecordsTableWidget->setAlternatingRowColors(true);
    ui->recentRecordsTableWidget->setRowCount(0);
    ui->recentRecordsTableWidget->setToolTip(QStringLiteral("双击一条记录可回看对应原图。"));

    ui->statusValueLabel->setText(QStringLiteral("系统已就绪"));
    ui->resultStateValueLabel->setText(QStringLiteral("--"));
    ui->defectCountValueLabel->setText(QStringLiteral("--"));
    ui->processTimeValueLabel->setText(QStringLiteral("--"));
    ui->activeRecipeValueLabel->setText(QStringLiteral("default-aoi"));
    ui->summaryTextValueLabel->setText(QStringLiteral("--"));
    ui->currentImageValueLabel->setText(QStringLiteral("未选择"));
    ui->roiValueLabel->setText(QStringLiteral("未设置"));
    statusBar()->showMessage(tr("系统已启动"), 3000);
    updateRoiSummary();
    updateInputSourceUi();
    onInspectionRunningChanged(false);
    syncCaptureState();
    syncTcpState();
}

void MainWindow::syncCaptureState()
{
    // 采集态同步：根据输入模式、预览态、检测态统一计算按钮可用性。
    const CaptureStatusSnapshot &status = m_controller->captureStatus();
    const InputSourceConfig config = displayInputSourceConfig();
    const bool isFileMode = config.type == InputSourceType::FileImage;
    const bool isRunning = m_controller->isInspectionRunning();
    const bool cancelRequested = m_controller->isInspectionCancelRequested();
    const bool previewing = status.state == CaptureState::Previewing;
    const bool continuousEnabled = m_controller->isContinuousInspectionEnabled();
    const bool captureTransitioning =
        status.state == CaptureState::Opening || status.state == CaptureState::Closing;
    const bool sourceControlsLocked =
        isRunning || cancelRequested || continuousEnabled || captureTransitioning;
    const bool allowHistoryReview = !sourceControlsLocked && !status.opened;

    ui->importImageButton->setEnabled(!sourceControlsLocked && !m_controller->captureStatus().opened);
    ui->browseInputSourceButton->setEnabled(!sourceControlsLocked && !status.opened);
    ui->inputSourceTypeComboBox->setEnabled(!sourceControlsLocked && !status.opened);
    ui->inputSourcePathLineEdit->setEnabled(!sourceControlsLocked && !status.opened);
    ui->cameraDeviceSpinBox->setEnabled(!sourceControlsLocked && !status.opened);
    ui->previewIntervalSpinBox->setEnabled(!sourceControlsLocked && !status.opened);
    ui->continuousInspectionIntervalSpinBox->setEnabled(!isRunning && !cancelRequested && !continuousEnabled);
    ui->openInputSourceButton->setEnabled(!sourceControlsLocked && !status.opened && status.state != CaptureState::Opening);
    ui->closeInputSourceButton->setEnabled(!sourceControlsLocked && (status.opened || status.state == CaptureState::Error));
    ui->startPreviewButton->setEnabled(!sourceControlsLocked && status.opened && !previewing);
    ui->stopPreviewButton->setEnabled(!sourceControlsLocked && previewing);
    ui->startContinuousInspectionButton->setEnabled(
        !isRunning
        && !continuousEnabled
        && !isFileMode
        && previewing
        && m_controller->hasLatestFrame());
    ui->stopContinuousInspectionButton->setEnabled(continuousEnabled);
    ui->recentRecordsTableWidget->setEnabled(allowHistoryReview);

    if (isFileMode) {
        ui->startInspectionButton->setEnabled(
            !sourceControlsLocked
            && (!m_currentImagePath.isEmpty() || !ui->inputSourcePathLineEdit->text().trimmed().isEmpty()));
    } else {
        ui->startInspectionButton->setEnabled(
            !sourceControlsLocked && status.state == CaptureState::Previewing && m_controller->hasLatestFrame());
        if (!m_controller->hasLatestFrame()) {
            ui->currentImageValueLabel->setText(status.statusText.isEmpty()
                                                    ? QStringLiteral("等待输入源帧")
                                                    : status.statusText);
        }
    }
}

void MainWindow::syncTcpState()
{
    // TCP 状态展示保持轻量：只同步状态文本和连接按钮文案。
    const QString statusText = m_controller->tcpStatusText();
    const bool tcpPending = m_controller->isTcpOperationPending();
    const bool detectionLocksTcp =
        m_controller->isInspectionRunning() || m_controller->isContinuousInspectionEnabled();

    ui->tcpStatusValueLabel->setText(statusText);
    if (tcpPending) {
        if (statusText.startsWith(QStringLiteral("连接中"))) {
            ui->tcpConnectButton->setText(QStringLiteral("连接中..."));
        } else if (statusText.startsWith(QStringLiteral("断开中"))) {
            ui->tcpConnectButton->setText(QStringLiteral("断开中..."));
        } else {
            ui->tcpConnectButton->setText(QStringLiteral("处理中..."));
        }
    } else {
        ui->tcpConnectButton->setText(m_controller->isTcpConnected() ? QStringLiteral("断开连接")
                                                                      : QStringLiteral("连接 TCP"));
    }
    ui->tcpConnectButton->setEnabled(!detectionLocksTcp && !tcpPending);
    ui->tcpIpLineEdit->setEnabled(!detectionLocksTcp && !tcpPending);
    ui->tcpPortSpinBox->setEnabled(!detectionLocksTcp && !tcpPending);
}

void MainWindow::onLogFilterChanged()
{
    // 过滤条件变化后实时重绘日志视图。
    refreshLogView();
}

void MainWindow::onUiLogGenerated(const LogEvent &event)
{
    // 界面日志采用固定容量环形裁剪，避免长时运行导致内存持续增长。
    m_uiLogEvents.append(event);
    while (m_uiLogEvents.size() > kUiLogHistoryLimit) {
        m_uiLogEvents.removeFirst();
    }

    ensureLogModuleOption(event.module);
    refreshLogView();
}
