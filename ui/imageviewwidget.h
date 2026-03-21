#pragma once

#include <QImage>
#include <QLabel>

class QResizeEvent;

class ImageViewWidget : public QLabel
{
    Q_OBJECT

public:
    explicit ImageViewWidget(QWidget *parent = nullptr);

    void setPlaceholderText(const QString &text);
    void setImage(const QImage &image);
    void clearImage();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void refreshPixmap();

    QImage m_image;
    QString m_placeholder;
};

