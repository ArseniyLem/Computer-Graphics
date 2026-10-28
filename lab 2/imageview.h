// ============================================================================
//  imageview.h — QScrollArea + QLabel для просмотра изображения.
//  Режимы: «по размеру окна» (с сохранением пропорций) и 100 % с прокруткой.
//  Класс без сигналов/слотов, поэтому Q_OBJECT не нужен.
// ============================================================================
#pragma once

#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QResizeEvent>
#include <QScrollArea>

class ImageView : public QScrollArea {
public:
    explicit ImageView(QWidget* parent = nullptr)
        : QScrollArea(parent), m_label(new QLabel)
    {
        m_label->setAlignment(Qt::AlignCenter);
        setWidget(m_label);
        setWidgetResizable(false);
        setAlignment(Qt::AlignCenter);
        setMinimumSize(200, 200);
        setFit(true);
    }

    void setImage(const QImage& image)
    {
        m_image = image;
        refresh();
    }

    void clear() { setImage(QImage()); }

    void setFit(bool fit)
    {
        m_fit = fit;
        const auto policy = fit ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded;
        setHorizontalScrollBarPolicy(policy);
        setVerticalScrollBarPolicy(policy);
        refresh();
    }

protected:
    void resizeEvent(QResizeEvent* e) override
    {
        QScrollArea::resizeEvent(e);
        if (m_fit)
            refresh();
    }

private:
    void refresh()
    {
        if (m_image.isNull()) {
            m_label->setPixmap(QPixmap());
            m_label->setText(QStringLiteral("—"));
            m_label->resize(viewport()->size());
            return;
        }
        QPixmap pm = QPixmap::fromImage(m_image);
        if (m_fit) {
            pm = pm.scaled(viewport()->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        m_label->setPixmap(pm);
        m_label->resize(pm.size());
    }

    QLabel* m_label;
    QImage  m_image;
    bool    m_fit = true;
};
