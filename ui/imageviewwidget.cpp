// 表现层：imageviewwidget.cpp 负责界面交互与状态展示。
// 本文件位于巡检流程展示端，承接用户操作与结果回显。
#include "ui/imageviewwidget.h"

#include <QPixmap>
#include <QResizeEvent>
#include <QSizePolicy>

ImageViewWidget::ImageViewWidget(QWidget *parent)
    : QLabel(parent)
    , m_placeholder(QStringLiteral("等待图像"))
{
    // 图像控件默认可扩展，保持和主工作区拉伸策略一致。
    setAlignment(Qt::AlignCenter);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(320, 240);
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Sunken);
    setWordWrap(true);
    refreshPixmap();
}

void ImageViewWidget::setPlaceholderText(const QString &text)
{
    m_placeholder = text;
    if (m_image.isNull()) {
        refreshPixmap();
    }
}

void ImageViewWidget::setImage(const QImage &image)
{
    m_image = image;
    refreshPixmap();
}

void ImageViewWidget::clearImage()
{
    m_image = QImage{};
    refreshPixmap();
}

void ImageViewWidget::resizeEvent(QResizeEvent *event)
{
    QLabel::resizeEvent(event);
    // 控件尺寸变化时按当前尺寸重新缩放显示图像。
    refreshPixmap();
}

void ImageViewWidget::refreshPixmap()
{
    if (m_image.isNull()) {
        // 无图时只显示占位文本，保持空态可读。
        setPixmap(QPixmap{});
        setText(m_placeholder);
        return;
    }

    // 有图时保持原图比例缩放，避免图像内容被拉伸变形。
    setText(QString{});
    setPixmap(QPixmap::fromImage(m_image).scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
