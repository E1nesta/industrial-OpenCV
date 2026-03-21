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
#include <QStatusBar>
#include <QTableWidgetItem>
#include <QTextDocument>
#include <QVBoxLayout>

#include "app/appcontroller.h"
#include "common/utils.h"
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
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("选择检测图片"),
        QString(),
        tr("图片文件 (*.png *.jpg *.jpeg *.bmp)"));

    if (filePath.isEmpty()) {
        return;
    }

    const QImage image(filePath);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("图片加载失败"), tr("所选文件无法作为图片加载。"));
        m_controller->logManager().warn(QStringLiteral("界面"), QStringLiteral("图片加载失败：%1").arg(filePath));
        return;
    }

    m_currentImagePath = filePath;
    m_sourceImageView->setImage(image);
    m_resultImageView->clearImage();
    ui->currentImageValueLabel->setText(filePath);
    ui->resultStateValueLabel->setText(QStringLiteral("就绪"));
    ui->defectCountValueLabel->setText(QStringLiteral("--"));
    ui->processTimeValueLabel->setText(QStringLiteral("--"));

    m_controller->logManager().info(QStringLiteral("界面"), QStringLiteral("已导入图片：%1").arg(filePath));
    statusBar()->showMessage(tr("图片已加载"), 3000);
}

void MainWindow::onStartDetectionClicked()
{
    if (m_currentImagePath.isEmpty()) {
        QMessageBox::information(this, tr("尚未选择图片"), tr("请先导入一张待检测图片。"));
        return;
    }

    m_controller->setVisionParam(collectVisionParam());
    if (!m_controller->startDetection(m_currentImagePath)) {
        if (!m_controller->isDetectionRunning()) {
            QMessageBox::warning(this, tr("检测未启动"), m_controller->statusMessage());
        }
        return;
    }
}

void MainWindow::onStopDetectionClicked()
{
    if (!m_controller->cancelDetection()) {
        return;
    }

    ui->resultStateValueLabel->setText(QStringLiteral("取消中"));
    statusBar()->showMessage(tr("正在取消检测任务"), 3000);
}

void MainWindow::onExportRecordsClicked()
{
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
    stream << "timestamp,batch_no,result,defect_count,process_time_ms,image_path,result_image_path\n";

    for (const InspectionRecord &record : m_recentRecords) {
        stream << escapeCsvField(record.timestamp) << ','
               << escapeCsvField(record.batchNo) << ','
               << escapeCsvField(utils::boolToResultText(record.isOk)) << ','
               << record.defectCount << ','
               << QString::number(record.processTimeMs, 'f', 2) << ','
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
    Q_UNUSED(column);

    if (row < 0 || row >= m_recentRecords.size()) {
        return;
    }

    displayRecordDetails(m_recentRecords.at(row));
}

void MainWindow::onLoadParamClicked()
{
    m_controller->reloadConfig();
}

void MainWindow::onSaveParamClicked()
{
    m_controller->setVisionParam(collectVisionParam());
    m_controller->setDeviceConfig(collectDeviceConfig());
    m_controller->saveCurrentParam();
}

void MainWindow::onResetParamClicked()
{
    m_controller->resetToDefaults();
}

void MainWindow::onTcpConnectClicked()
{
    m_controller->setDeviceConfig(collectDeviceConfig());

    if (m_controller->isTcpConnected()) {
        m_controller->disconnectTcpDevice();
        return;
    }

    if (!m_controller->connectTcpDevice()) {
        QMessageBox::warning(this, tr("TCP 连接失败"), m_controller->tcpStatusText());
    }
}

void MainWindow::onDetectionStarted()
{
    m_resultImageView->clearImage();
    ui->resultStateValueLabel->setText(QStringLiteral("检测中"));
    ui->defectCountValueLabel->setText(QStringLiteral("--"));
    ui->processTimeValueLabel->setText(QStringLiteral("--"));
    statusBar()->showMessage(tr("检测任务执行中"), 3000);
}

void MainWindow::onDetectionFinished(const DetectResult &result, const QImage &resultImage)
{
    m_resultImageView->setImage(resultImage);
    ui->resultStateValueLabel->setText(utils::boolToResultText(result.isOk));
    ui->defectCountValueLabel->setText(QString::number(result.defectCount));
    ui->processTimeValueLabel->setText(QStringLiteral("%1 ms").arg(result.processTimeMs, 0, 'f', 2));
    statusBar()->showMessage(tr("检测完成"), 3000);
}

void MainWindow::onDetectionFailed(const QString &errorMessage)
{
    ui->resultStateValueLabel->setText(QStringLiteral("失败"));
    ui->defectCountValueLabel->setText(QStringLiteral("--"));
    ui->processTimeValueLabel->setText(QStringLiteral("--"));
    statusBar()->showMessage(tr("检测失败"), 3000);
    QMessageBox::warning(this, tr("检测失败"), errorMessage);
}

void MainWindow::onDetectionCanceled()
{
    ui->resultStateValueLabel->setText(QStringLiteral("已取消"));
    ui->defectCountValueLabel->setText(QStringLiteral("--"));
    ui->processTimeValueLabel->setText(QStringLiteral("--"));
    m_resultImageView->clearImage();
    statusBar()->showMessage(tr("检测任务已取消"), 3000);
}

void MainWindow::onDetectionRunningChanged(bool isRunning)
{
    const bool cancelRequested = m_controller->isDetectionCancelRequested();

    if (isRunning && cancelRequested) {
        ui->resultStateValueLabel->setText(QStringLiteral("取消中"));
    } else if (isRunning) {
        ui->resultStateValueLabel->setText(QStringLiteral("检测中"));
    }

    ui->startDetectionButton->setEnabled(!isRunning);
    ui->stopDetectionButton->setEnabled(isRunning && !cancelRequested);
    ui->stopDetectionButton->setText(cancelRequested ? QStringLiteral("取消中...") : QStringLiteral("停止检测"));
    ui->importImageButton->setEnabled(!isRunning);
    ui->loadParamButton->setEnabled(!isRunning);
    ui->saveParamButton->setEnabled(!isRunning);
    ui->resetParamButton->setEnabled(!isRunning);
    ui->tcpConnectButton->setEnabled(!isRunning);
    ui->thresholdSpinBox->setEnabled(!isRunning);
    ui->minAreaSpinBox->setEnabled(!isRunning);
    ui->maxAreaSpinBox->setEnabled(!isRunning);
    ui->morphologyCheckBox->setEnabled(!isRunning);
    ui->roiXSpinBox->setEnabled(!isRunning);
    ui->roiYSpinBox->setEnabled(!isRunning);
    ui->roiWidthSpinBox->setEnabled(!isRunning);
    ui->roiHeightSpinBox->setEnabled(!isRunning);
    ui->clearRoiButton->setEnabled(!isRunning);
    ui->imageSavePathLineEdit->setEnabled(!isRunning);
    ui->browseImageSavePathButton->setEnabled(!isRunning);
    ui->tcpIpLineEdit->setEnabled(!isRunning);
    ui->tcpPortSpinBox->setEnabled(!isRunning);
}

void MainWindow::onControllerStatusChanged(const QString &message)
{
    ui->statusValueLabel->setText(message);
    statusBar()->showMessage(message, 5000);
}

void MainWindow::syncFromController()
{
    const VisionParam &param = m_controller->visionParam();
    const DeviceConfig &deviceConfig = m_controller->deviceConfig();

    ui->thresholdSpinBox->setValue(param.threshold);
    ui->minAreaSpinBox->setValue(param.minArea);
    ui->maxAreaSpinBox->setValue(param.maxArea);
    ui->morphologyCheckBox->setChecked(param.enableMorphology);
    setRoiControls(param.roi);
    ui->imageSavePathLineEdit->setText(param.imageSavePath);
    ui->tcpIpLineEdit->setText(deviceConfig.ip);
    ui->tcpPortSpinBox->setValue(deviceConfig.port);
    {
        const QSignalBlocker blocker(ui->logCaptureLevelComboBox);
        ui->logCaptureLevelComboBox->setCurrentText(m_controller->logManager().minimumLevelName());
    }

    ui->stageValueLabel->setText(m_controller->projectStage());
    ui->configPathValueLabel->setText(m_controller->configFilePath());

    if (!m_controller->statusMessage().isEmpty()) {
        ui->statusValueLabel->setText(m_controller->statusMessage());
    }

    onDetectionRunningChanged(m_controller->isDetectionRunning());
    syncTcpState();
}

void MainWindow::syncRecentRecords()
{
    updateRecentRecordsTable(m_controller->recentRecords());
}

VisionParam MainWindow::collectVisionParam() const
{
    VisionParam param = m_controller->visionParam();
    param.threshold = ui->thresholdSpinBox->value();
    param.minArea = ui->minAreaSpinBox->value();
    param.maxArea = ui->maxAreaSpinBox->value();
    param.enableMorphology = ui->morphologyCheckBox->isChecked();
    param.roi = collectRoi();
    const QString imageSavePath = ui->imageSavePathLineEdit->text().trimmed();
    param.imageSavePath = imageSavePath.isEmpty() ? VisionParam{}.imageSavePath : imageSavePath;
    return param;
}

DeviceConfig MainWindow::collectDeviceConfig() const
{
    DeviceConfig config = m_controller->deviceConfig();
    config.ip = ui->tcpIpLineEdit->text().trimmed();
    config.port = ui->tcpPortSpinBox->value();
    return config;
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
    ui->currentImageValueLabel->setText(record.imagePath);
    ui->resultStateValueLabel->setText(utils::boolToResultText(record.isOk));
    ui->defectCountValueLabel->setText(QString::number(record.defectCount));
    ui->processTimeValueLabel->setText(QStringLiteral("%1 ms").arg(record.processTimeMs, 0, 'f', 2));
    ui->selectedRecordDetailValueLabel->setText(
        QStringLiteral("时间：%1\n批次：%2\n结果：%3\n缺陷数：%4\n耗时：%5 ms\n原图：%6\n结果图：%7")
            .arg(record.timestamp,
                 record.batchNo.isEmpty() ? QStringLiteral("未设置") : record.batchNo,
                 utils::boolToResultText(record.isOk))
            .arg(record.defectCount)
            .arg(record.processTimeMs, 0, 'f', 2)
            .arg(record.imagePath,
                 record.resultImagePath.isEmpty() ? QStringLiteral("未归档") : record.resultImagePath));

    m_resultImageView->clearImage();

    const QFileInfo imageInfo(record.imagePath);
    if (!imageInfo.exists()) {
        m_currentImagePath.clear();
        m_sourceImageView->clearImage();
        m_controller->logManager().warn(
            QStringLiteral("界面"),
            QStringLiteral("历史记录图片不存在：%1").arg(record.imagePath));
        statusBar()->showMessage(tr("历史记录图片不存在"), 3000);
        return;
    }

    const QImage image(record.imagePath);
    if (image.isNull()) {
        m_currentImagePath.clear();
        m_sourceImageView->clearImage();
        m_controller->logManager().warn(
            QStringLiteral("界面"),
            QStringLiteral("历史记录图片无法加载：%1").arg(record.imagePath));
        statusBar()->showMessage(tr("历史记录图片无法加载"), 3000);
        return;
    }

    m_currentImagePath = record.imagePath;
    m_sourceImageView->setImage(image);

    const QFileInfo resultImageInfo(record.resultImagePath);
    if (resultImageInfo.exists()) {
        const QImage resultImage(record.resultImagePath);
        if (!resultImage.isNull()) {
            m_resultImageView->setImage(resultImage);
        } else {
            m_controller->logManager().warn(
                QStringLiteral("界面"),
                QStringLiteral("历史结果图无法加载：%1").arg(record.resultImagePath));
        }
    } else if (!record.resultImagePath.isEmpty()) {
        m_controller->logManager().warn(
            QStringLiteral("界面"),
            QStringLiteral("历史结果图不存在：%1").arg(record.resultImagePath));
    }

    m_controller->logManager().info(
        QStringLiteral("界面"),
        QStringLiteral("已切换到历史记录：%1").arg(record.imagePath));
    statusBar()->showMessage(tr("已加载历史记录原图"), 3000);
}

void MainWindow::refreshLogView()
{
    ui->logPlainTextEdit->clear();

    for (const LogEvent &event : m_uiLogEvents) {
        if (logMatchesFilters(event)) {
            appendLog(event.formattedLine);
        }
    }
}

bool MainWindow::logMatchesFilters(const LogEvent &event) const
{
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
    if (module.isEmpty() || m_knownLogModules.contains(module)) {
        return;
    }

    m_knownLogModules.append(module);
    ui->logModuleFilterComboBox->addItem(module);
}

void MainWindow::appendLog(const QString &message)
{
    ui->logPlainTextEdit->appendPlainText(message);
}

void MainWindow::updateRecentRecordsTable(const QList<InspectionRecord> &records)
{
    m_recentRecords = records;
    ui->recentRecordsTableWidget->clearContents();
    ui->recentRecordsTableWidget->setRowCount(records.size());

    for (int row = 0; row < records.size(); ++row) {
        const InspectionRecord &record = records.at(row);
        auto *timestampItem = new QTableWidgetItem(record.timestamp);
        auto *resultItem = new QTableWidgetItem(utils::boolToResultText(record.isOk));
        auto *defectItem = new QTableWidgetItem(QString::number(record.defectCount));
        auto *timeItem = new QTableWidgetItem(QStringLiteral("%1").arg(record.processTimeMs, 0, 'f', 2));
        auto *imageItem = new QTableWidgetItem(QFileInfo(record.imagePath).fileName());
        const QString tooltip = QStringLiteral("双击回看\n%1").arg(record.imagePath);

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

    if (records.isEmpty()) {
        ui->selectedRecordDetailValueLabel->setText(QStringLiteral("暂无历史记录。"));
    }
}

void MainWindow::setRoiControls(const QRect &roi)
{
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
    ui->roiValueLabel->setText(utils::formatRoi(collectRoi()));
}

void MainWindow::bindSignals()
{
    connect(ui->importImageButton, &QPushButton::clicked, this, &MainWindow::onImportImageClicked);
    connect(ui->startDetectionButton, &QPushButton::clicked, this, &MainWindow::onStartDetectionClicked);
    connect(ui->stopDetectionButton, &QPushButton::clicked, this, &MainWindow::onStopDetectionClicked);
    connect(ui->loadParamButton, &QPushButton::clicked, this, &MainWindow::onLoadParamClicked);
    connect(ui->saveParamButton, &QPushButton::clicked, this, &MainWindow::onSaveParamClicked);
    connect(ui->resetParamButton, &QPushButton::clicked, this, &MainWindow::onResetParamClicked);
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
        ui->logCaptureLevelComboBox,
        &QComboBox::currentTextChanged,
        this,
        &MainWindow::onRuntimeLogLevelChanged);
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
    connect(&m_controller->logManager(), &LogManager::uiLogGenerated, this, &MainWindow::onUiLogGenerated);
    connect(
        &m_controller->logManager(),
        &LogManager::minimumLevelChanged,
        this,
        [this](const QString &levelName) {
            const QSignalBlocker blocker(ui->logCaptureLevelComboBox);
            ui->logCaptureLevelComboBox->setCurrentText(levelName);
            statusBar()->showMessage(
                tr("运行时日志级别已切换为 %1").arg(levelName),
                3000);
        });

    connect(m_controller, &AppController::detectionStarted, this, &MainWindow::onDetectionStarted);
    connect(m_controller, &AppController::detectionFinished, this, &MainWindow::onDetectionFinished);
    connect(m_controller, &AppController::detectionFailed, this, &MainWindow::onDetectionFailed);
    connect(m_controller, &AppController::detectionCanceled, this, &MainWindow::onDetectionCanceled);
    connect(
        m_controller,
        &AppController::detectionRunningChanged,
        this,
        &MainWindow::onDetectionRunningChanged);
    connect(m_controller, &AppController::statusChanged, this, &MainWindow::onControllerStatusChanged);
    connect(m_controller, &AppController::visionParamChanged, this, &MainWindow::syncFromController);
    connect(m_controller, &AppController::deviceConfigChanged, this, &MainWindow::syncFromController);
    connect(m_controller, &AppController::recordsChanged, this, &MainWindow::syncRecentRecords);
    connect(m_controller, &AppController::tcpStateChanged, this, &MainWindow::syncTcpState);
}

void MainWindow::setupImageViews()
{
    auto *sourceLayout = new QVBoxLayout(ui->sourceImageHost);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    m_sourceImageView = new ImageViewWidget(ui->sourceImageHost);
    m_sourceImageView->setPlaceholderText(QStringLiteral("等待导入原始图像"));
    sourceLayout->addWidget(m_sourceImageView);

    auto *resultLayout = new QVBoxLayout(ui->resultImageHost);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    m_resultImageView = new ImageViewWidget(ui->resultImageHost);
    m_resultImageView->setPlaceholderText(QStringLiteral("等待检测结果图"));
    resultLayout->addWidget(m_resultImageView);
}

void MainWindow::setupUiState()
{
    ui->logPlainTextEdit->document()->setMaximumBlockCount(200);
    ui->logCaptureLevelComboBox->addItems(
        QStringList{QStringLiteral("DEBUG"),
                    QStringLiteral("INFO"),
                    QStringLiteral("WARN"),
                    QStringLiteral("ERROR")});
    ui->logCaptureLevelComboBox->setToolTip(
        QStringLiteral("控制运行时实际采集的最低日志级别，会影响文件、控制台和界面日志源。"));
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
    ui->currentImageValueLabel->setText(QStringLiteral("未选择"));
    ui->selectedRecordDetailValueLabel->setText(QStringLiteral("双击左侧历史记录可查看详情并回看原图。"));
    ui->roiValueLabel->setText(QStringLiteral("未设置"));
    ui->bottomSplitter->setStretchFactor(0, 3);
    ui->bottomSplitter->setStretchFactor(1, 2);
    statusBar()->showMessage(tr("系统已启动"), 3000);
    updateRoiSummary();
    onDetectionRunningChanged(false);
    syncTcpState();
}

void MainWindow::syncTcpState()
{
    ui->tcpStatusValueLabel->setText(m_controller->tcpStatusText());
    ui->tcpConnectButton->setText(m_controller->isTcpConnected() ? QStringLiteral("断开连接")
                                                                  : QStringLiteral("连接 TCP"));
}

void MainWindow::onLogFilterChanged()
{
    refreshLogView();
}

void MainWindow::onRuntimeLogLevelChanged(const QString &levelName)
{
    m_controller->logManager().setMinimumLevelName(levelName);
}

void MainWindow::onUiLogGenerated(const LogEvent &event)
{
    m_uiLogEvents.append(event);
    while (m_uiLogEvents.size() > kUiLogHistoryLimit) {
        m_uiLogEvents.removeFirst();
    }

    ensureLogModuleOption(event.module);
    refreshLogView();
}
