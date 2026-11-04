#include "canvaswidget.h"

#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

/// Наименьший "красивый" шаг из ряда 1, 2, 5, 10, 20, 50, ... не меньше minWorld.
int niceStep(double minWorld)
{
    if (minWorld <= 1.0)
        return 1;
    const double mag = std::pow(10.0, std::floor(std::log10(minWorld)));
    const double mult[] = {1.0, 2.0, 5.0, 10.0};
    for (double m : mult)
        if (m * mag >= minWorld)
            return static_cast<int>(m * mag);
    return static_cast<int>(10.0 * mag);
}

/// Наименьшее кратное step, не меньшее v.
int ceilToStep(double v, int step)
{
    return static_cast<int>(std::ceil(v / step)) * step;
}

} // namespace

CanvasWidget::CanvasWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(400, 300);
    setCursor(Qt::OpenHandCursor);
}

void CanvasWidget::setPixels(const QVector<QPoint> &pixels, int totalCount, int highlightFrom)
{
    m_pixels = pixels;
    m_totalPixels = totalCount;
    m_highlightFrom = highlightFrom;
    update();
}

void CanvasWidget::setIdealLine(const QPoint &a, const QPoint &b)
{
    m_shape = Shape::Line;
    m_a = a;
    m_b = b;
    update();
}

void CanvasWidget::setIdealCircle(const QPoint &center, int radius)
{
    m_shape = Shape::Circle;
    m_a = center;
    m_radius = radius;
    update();
}

void CanvasWidget::clearAll()
{
    m_pixels.clear();
    m_shape = Shape::None;
    update();
}

void CanvasWidget::setShowIdeal(bool on)
{
    m_showIdeal = on;
    update();
}

void CanvasWidget::resetView()
{
    m_scale = 24.0;
    m_origin = QPointF(width() / 2.0, height() / 2.0);
    m_viewInitialized = true;
    update();
}

void CanvasWidget::fitToShape()
{
    if (m_pixels.isEmpty())
        return;

    int minX = m_pixels[0].x(), maxX = minX;
    int minY = m_pixels[0].y(), maxY = minY;
    for (const QPoint &p : m_pixels) {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }

    // Поля вокруг фигуры - по 2 клетки с каждой стороны.
    const double bw = (maxX - minX) + 5.0;
    const double bh = (maxY - minY) + 5.0;
    const double s = std::min(width() / bw, height() / bh);
    m_scale = std::clamp(s, 4.0, 60.0);

    const double cxWorld = (minX + maxX) / 2.0;
    const double cyWorld = (minY + maxY) / 2.0;
    m_origin = QPointF(width() / 2.0 - cxWorld * m_scale,
                       height() / 2.0 + cyWorld * m_scale);
    update();
}

QPointF CanvasWidget::toScreen(const QPointF &w) const
{
    return QPointF(m_origin.x() + w.x() * m_scale,
                   m_origin.y() - w.y() * m_scale);
}

QPointF CanvasWidget::toWorld(const QPointF &s) const
{
    return QPointF((s.x() - m_origin.x()) / m_scale,
                   (m_origin.y() - s.y()) / m_scale);
}

void CanvasWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_viewInitialized)
        resetView();
}

void CanvasWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), Qt::white);

    const double w = width(), h = height();
    const QPointF wMin = toWorld(QPointF(0, h));  // левый нижний угол в мировых координатах
    const QPointF wMax = toWorld(QPointF(w, 0));  // правый верхний угол
    const double xmin = wMin.x(), xmax = wMax.x();
    const double ymin = wMin.y(), ymax = wMax.y();

    // Шаг подписей: подписи не чаще, чем раз в ~55 экранных пикселей.
    const int labelStep = niceStep(55.0 / m_scale);
    // При крупном масштабе сетка рисуется в каждом узле, иначе - только по шагу подписей.
    const int gridStep = (m_scale >= 8.0) ? 1 : labelStep;

    // --- Линии сетки (проходят через центры пикселей, т.е. через целые узлы) ---
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(QColor(224, 224, 224), 1));
    for (int x = ceilToStep(xmin, gridStep); x <= xmax; x += gridStep) {
        const double sx = toScreen(QPointF(x, 0)).x();
        p.drawLine(QPointF(sx, 0), QPointF(sx, h));
    }
    for (int y = ceilToStep(ymin, gridStep); y <= ymax; y += gridStep) {
        const double sy = toScreen(QPointF(0, y)).y();
        p.drawLine(QPointF(0, sy), QPointF(w, sy));
    }

    // --- Растровые точки: квадрат 1x1 с центром в узле сетки ---
    const double s = m_scale;
    const bool drawOutline = s >= 10.0;
    const bool drawLabels = s >= 44.0;
    QFont smallFont = font();
    smallFont.setPointSize(7);

    for (int i = 0; i < m_pixels.size(); ++i) {
        const QPointF c = toScreen(QPointF(m_pixels[i]));
        if (c.x() < -s || c.x() > w + s || c.y() < -s || c.y() > h + s)
            continue; // за пределами видимой области

        QColor color(235, 90, 70, 210);
        const int total = (m_totalPixels >= 0) ? m_totalPixels : int(m_pixels.size());
        if (m_shape == Shape::Line && !m_pixels.isEmpty()) {
            if (i == 0)
                color = QColor(60, 180, 75, 220);                   // начало - зелёный
            else if (i == total - 1)
                color = QColor(240, 160, 30, 230);                  // конец - оранжевый
        }
        if (m_highlightFrom >= 0 && i >= m_highlightFrom)
            color = QColor(255, 215, 0, 240);                       // последний шаг - жёлтый

        if (s >= 3.0) {
            const QRectF cell(c.x() - s / 2, c.y() - s / 2, s, s);
            p.fillRect(cell, color);
            if (drawOutline) {
                p.setPen(QPen(QColor(120, 40, 30), 1));
                p.drawRect(cell);
            }
        } else {
            p.fillRect(QRectF(c.x() - 1, c.y() - 1, 2, 2), color);
        }

        if (drawOutline) { // маркер центра пикселя = узел сетки
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(60, 20, 20));
            p.drawEllipse(c, 1.8, 1.8);
            p.setBrush(Qt::NoBrush);
        }
        if (drawLabels) {
            p.setFont(smallFont);
            p.setPen(QColor(50, 50, 50));
            p.drawText(QRectF(c.x() - s / 2, c.y() - s / 2 + 2, s, 12), Qt::AlignHCenter | Qt::AlignTop,
                       QString("%1;%2").arg(m_pixels[i].x()).arg(m_pixels[i].y()));
        }
    }

    // --- Идеальная (математическая) фигура поверх пикселей ---
    p.setRenderHint(QPainter::Antialiasing, true);
    if (m_showIdeal && m_shape != Shape::None) {
        p.setPen(QPen(QColor(30, 90, 220), 1.5));
        p.setBrush(Qt::NoBrush);
        if (m_shape == Shape::Line) {
            p.drawLine(toScreen(QPointF(m_a)), toScreen(QPointF(m_b)));
        } else {
            const double rr = m_radius * m_scale;
            p.drawEllipse(toScreen(QPointF(m_a)), rr, rr);
        }
    }

    // --- Оси координат ---
    const QPointF o = toScreen(QPointF(0, 0));
    const bool xAxisVisible = o.y() >= 0 && o.y() <= h;
    const bool yAxisVisible = o.x() >= 0 && o.x() <= w;
    const QColor axisColor(20, 20, 20);
    p.setPen(QPen(axisColor, 1.6));
    p.setBrush(axisColor);
    if (xAxisVisible) {
        p.drawLine(QPointF(0, o.y()), QPointF(w, o.y()));
        p.drawPolygon(QPolygonF({QPointF(w, o.y()), QPointF(w - 10, o.y() - 4), QPointF(w - 10, o.y() + 4)}));
    }
    if (yAxisVisible) {
        p.drawLine(QPointF(o.x(), 0), QPointF(o.x(), h));
        p.drawPolygon(QPolygonF({QPointF(o.x(), 0), QPointF(o.x() - 4, 10), QPointF(o.x() + 4, 10)}));
    }
    p.setBrush(Qt::NoBrush);

    // --- Подписи делений ---
    QFont f = font();
    f.setPointSize(8);
    p.setFont(f);
    const QFontMetrics fm(f);
    p.setPen(QColor(40, 40, 40));

    const double textY = std::clamp(o.y() + 14.0, 14.0, h - 4.0);
    for (int x = ceilToStep(xmin, labelStep); x <= xmax; x += labelStep) {
        const double sx = toScreen(QPointF(x, 0)).x();
        const QString t = QString::number(x);
        if (xAxisVisible)
            p.drawLine(QPointF(sx, o.y() - 3), QPointF(sx, o.y() + 3));
        p.drawText(QPointF(sx - fm.horizontalAdvance(t) / 2.0, textY), t);
    }
    const double textX = std::clamp(o.x() + 6.0, 2.0, w - 40.0);
    for (int y = ceilToStep(ymin, labelStep); y <= ymax; y += labelStep) {
        if (y == 0)
            continue; // "0" уже подписан на оси X
        const double sy = toScreen(QPointF(0, y)).y();
        if (yAxisVisible)
            p.drawLine(QPointF(o.x() - 3, sy), QPointF(o.x() + 3, sy));
        p.drawText(QPointF(textX, sy + 4), QString::number(y));
    }

    // Названия осей
    QFont bold = f;
    bold.setBold(true);
    bold.setPointSize(10);
    p.setFont(bold);
    p.drawText(QPointF(w - 16, std::clamp(o.y() - 8.0, 14.0, h - 20.0)), "X");
    p.drawText(QPointF(std::clamp(o.x() + 10.0, 2.0, w - 16.0), 14), "Y");

    // Масштаб
    p.setFont(f);
    p.setPen(QColor(90, 90, 90));
    p.drawText(QPointF(6, h - 6), QString("масштаб: %1 px/ед.").arg(m_scale, 0, 'f', 1));
}

void CanvasWidget::wheelEvent(QWheelEvent *event)
{
    // Зум относительно курсора: мировая точка под курсором остаётся на месте.
    const QPointF pos = event->position();
    const QPointF worldUnderCursor = toWorld(pos);

    const double factor = std::pow(1.15, event->angleDelta().y() / 120.0);
    m_scale = std::clamp(m_scale * factor, 2.0, 200.0);

    m_origin.setX(pos.x() - worldUnderCursor.x() * m_scale);
    m_origin.setY(pos.y() + worldUnderCursor.y() * m_scale);
    update();
    event->accept();
}

void CanvasWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_lastMouse = event->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
    }
}

void CanvasWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        m_origin += QPointF(event->position().toPoint() - m_lastMouse);
        m_lastMouse = event->position().toPoint();
        update();
    }
    const QPointF wp = toWorld(event->position());
    emit cursorCellChanged(QPoint(static_cast<int>(std::floor(wp.x() + 0.5)),
                                  static_cast<int>(std::floor(wp.y() + 0.5))));
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
    }
}
