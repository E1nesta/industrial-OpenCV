// 表现层：imageviewwidget.h 负责界面交互与状态展示。
// 本文件位于巡检流程展示端，承接用户操作与结果回显。
#pragma once

#include <QImage>
#include <QLabel>

class QResizeEvent;

// ImageViewWidget 负责统一展示输入图/结果图与空态占位文本。
// 该控件只处理显示缩放，不参与检测和业务状态管理。
class ImageViewWidget : public QLabel
{
    Q_OBJECT

public:
    explicit ImageViewWidget(QWidget *parent = nullptr);

    // 占位文案与图像更新入口。
    void setPlaceholderText(const QString &text);
    void setImage(const QImage &image);
    void clearImage();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    // 根据当前尺寸和图像状态刷新 QLabel 内容。
    void refreshPixmap();

    // 当前显示图像与空态提示。
    QImage m_image;
    QString m_placeholder;
};
