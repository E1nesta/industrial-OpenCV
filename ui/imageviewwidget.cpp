#include "ui/imageviewwidget.h"

#include <QPixmap>
#include <QResizeEvent>

ImageViewWidget::ImageViewWidget(QWidget *parent)
    : QLabel(parent)
    , m_placeholder(QStringLiteral("等待图像"))
{
    setAlignment(Qt::AlignCenter);
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
    refreshPixmap();
}

void ImageViewWidget::refreshPixmap()
{
    if (m_image.isNull()) {
        setPixmap(QPixmap{});
        setText(m_placeholder);
        return;
    }

    setText(QString{});
    setPixmap(QPixmap::fromImage(m_image).scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
