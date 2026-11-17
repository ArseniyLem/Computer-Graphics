#include "CanvasWidget.h"
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QtMath>
#include <algorithm>

CanvasWidget::CanvasWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(500, 400);
    setCursor(Qt::OpenHandCursor);
}

void CanvasWidget::setData(const QVector<Seg>& segs, const Rect& window, const QVector<Pt>& polygon)
{
    m_segs = segs; m_window = window;
    m_poly = CyrusBeck::normalizeCCW(polygon);
    m_needFit = true;
    recompute();
}

void CanvasWidget::setMode(Mode m) { m_mode = m; recompute(); }

// Выполняет отсечение всех отрезков выбранным алгоритмом и формирует журнал
void CanvasWidget::recompute()
{
    m_results.clear();
    QString log = (m_mode == Part1)
        ? "ЧАСТЬ 1: алгоритм Сазерленда-Коэна\n"
        : "ЧАСТЬ 2: алгоритм Кируса-Бека (выпуклый многоугольник)\n";
    for (int i = 0; i < m_segs.size(); ++i) {
        const Seg& s = m_segs[i];
        ClipResult r = (m_mode == Part1) ? CohenSutherland::clip(s, m_window)
                                         : CyrusBeck::clip(s, m_poly);
        log += QString("\nОтрезок %1: (%2; %3)-(%4; %5)\n").arg(i + 1)
                   .arg(s.a.x).arg(s.a.y).arg(s.b.x).arg(s.b.y) + r.log;
        log += r.visible ? QString("  => ВИДИМАЯ ЧАСТЬ: (%1; %2)-(%3; %4)\n")
                               .arg(r.seg.a.x, 0, 'f', 3).arg(r.seg.a.y, 0, 'f', 3)
                               .arg(r.seg.b.x, 0, 'f', 3).arg(r.seg.b.y, 0, 'f', 3)
                         : QString("  => НЕВИДИМ\n");
        m_results.push_back(r);
    }
    emit logChanged(log);
    update();
}

// Подбор масштаба и сдвига: показать все данные и начало координат
void CanvasWidget::fitView()
{
    double minx = 0, maxx = 0, miny = 0, maxy = 0;
    auto add = [&](const Pt& p) { minx = std::min(minx, p.x); maxx = std::max(maxx, p.x);
                                  miny = std::min(miny, p.y); maxy = std::max(maxy, p.y); };
    for (const Seg& s : m_segs) { add(s.a); add(s.b); }
    add({m_window.xmin, m_window.ymin}); add({m_window.xmax, m_window.ymax});
    for (const Pt& p : m_poly) add(p);
    double bw = std::max(maxx - minx, 1e-6), bh = std::max(maxy - miny, 1e-6);
    const double margin = 40;
    m_scale = std::min((width() - 2 * margin) / bw, (height() - 2 * margin) / bh);
    if (m_scale <= 0) m_scale = 30;
    m_origin = { (width()  - bw * m_scale) / 2 - minx * m_scale,
                 (height() + bh * m_scale) / 2 + miny * m_scale };
    m_needFit = false;
}

void CanvasWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::white);
    if (m_needFit) fitView();
    drawGrid(p);
    drawClipRegion(p);
    drawSegments(p);
    drawLegend(p);
}

// Декартова система координат: сетка с «красивым» шагом (1, 2, 5 * 10^n), оси, подписи
void CanvasWidget::drawGrid(QPainter& p)
{
    double raw = 50.0 / m_scale;
    double pw = std::pow(10.0, std::floor(std::log10(raw)));
    double step = pw;
    for (double m : {1.0, 2.0, 5.0, 10.0}) { step = m * pw; if (step >= raw) break; }

    double xl = (0 - m_origin.x()) / m_scale, xr = (width() - m_origin.x()) / m_scale;
    double yb = (m_origin.y() - height()) / m_scale, yt = m_origin.y() / m_scale;

    p.setFont(QFont("Sans", 8));
    QPen grid(QColor(225, 225, 225), 1);
    for (int i = int(std::ceil(xl / step)); i <= int(std::floor(xr / step)); ++i) {
        double x = i * step, sx = m_origin.x() + x * m_scale;
        p.setPen(grid); p.drawLine(QPointF(sx, 0), QPointF(sx, height()));
        if (i != 0) {
            p.setPen(Qt::darkGray);
            double ly = std::clamp(m_origin.y() + 13, 12.0, height() - 4.0);
            p.drawText(QPointF(sx + 2, ly), QString::number(x, 'g', 6));
        }
    }
    for (int i = int(std::ceil(yb / step)); i <= int(std::floor(yt / step)); ++i) {
        double y = i * step, sy = m_origin.y() - y * m_scale;
        p.setPen(grid); p.drawLine(QPointF(0, sy), QPointF(width(), sy));
        if (i != 0) {
            p.setPen(Qt::darkGray);
            double lx = std::clamp(m_origin.x() + 4, 2.0, width() - 40.0);
            p.drawText(QPointF(lx, sy - 2), QString::number(y, 'g', 6));
        }
    }
    // Оси координат
    p.setPen(QPen(Qt::black, 1.5));
    p.drawLine(QPointF(0, m_origin.y()), QPointF(width(), m_origin.y()));
    p.drawLine(QPointF(m_origin.x(), 0), QPointF(m_origin.x(), height()));
    p.drawText(QPointF(width() - 14, std::clamp(m_origin.y() - 5, 12.0, height() - 4.0)), "x");
    p.drawText(QPointF(std::clamp(m_origin.x() + 6, 2.0, width() - 12.0), 12), "y");
    p.drawText(QPointF(m_origin.x() + 3, m_origin.y() + 13), "0");
    p.setPen(Qt::darkGray);
    p.drawText(QPointF(8, height() - 8), QString("шаг сетки = %1").arg(step));
}

// Отсекающее окно (Часть 1) или выпуклый многоугольник (Часть 2) - синий контур
void CanvasWidget::drawClipRegion(QPainter& p)
{
    p.setPen(QPen(QColor(20, 60, 220), 2.5));
    p.setBrush(QColor(20, 60, 220, 25));
    if (m_mode == Part1) {
        QPointF a = toScreen({m_window.xmin, m_window.ymax});
        QPointF b = toScreen({m_window.xmax, m_window.ymin});
        p.drawRect(QRectF(a, b));
    } else {
        QPolygonF poly;
        for (const Pt& v : m_poly) poly << toScreen(v);
        p.drawPolygon(poly);
    }
    p.setBrush(Qt::NoBrush);
}

// Исходные отрезки - серый пунктир; видимые части - ярко-красные
void CanvasWidget::drawSegments(QPainter& p)
{
    QPen dash(QColor(130, 130, 130), 1.5, Qt::DashLine);
    for (int i = 0; i < m_segs.size(); ++i) {
        QPointF a = toScreen(m_segs[i].a), b = toScreen(m_segs[i].b);
        p.setPen(dash); p.drawLine(a, b);
        p.setBrush(QColor(130, 130, 130)); p.drawEllipse(a, 2.5, 2.5); p.drawEllipse(b, 2.5, 2.5);
        p.setPen(QColor(90, 90, 90)); p.drawText(a + QPointF(5, -5), QString::number(i + 1));
    }
    for (const ClipResult& r : m_results) {
        if (!r.visible) continue;
        QPointF a = toScreen(r.seg.a), b = toScreen(r.seg.b);
        p.setPen(QPen(QColor(255, 20, 20), 3.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(a, b);
        p.setBrush(QColor(255, 20, 20)); p.setPen(Qt::NoPen);
        p.drawEllipse(a, 4, 4); p.drawEllipse(b, 4, 4);
    }
    p.setBrush(Qt::NoBrush);
}

void CanvasWidget::drawLegend(QPainter& p)
{
    p.setFont(QFont("Sans", 9));
    int x = 10, y = 10;
    p.setPen(QPen(QColor(20, 60, 220), 2.5)); p.drawLine(x, y + 6, x + 30, y + 6);
    p.setPen(Qt::black); p.drawText(x + 38, y + 11, m_mode == Part1 ? "отсекающее окно" : "выпуклый многоугольник");
    y += 18;
    p.setPen(QPen(QColor(130, 130, 130), 1.5, Qt::DashLine)); p.drawLine(x, y + 6, x + 30, y + 6);
    p.setPen(Qt::black); p.drawText(x + 38, y + 11, "исходные отрезки");
    y += 18;
    p.setPen(QPen(QColor(255, 20, 20), 3.5)); p.drawLine(x, y + 6, x + 30, y + 6);
    p.setPen(Qt::black); p.drawText(x + 38, y + 11, "видимые части");
}

void CanvasWidget::wheelEvent(QWheelEvent* e)
{
    double factor = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QPointF c = e->position();
#else
    QPointF c = e->posF();
#endif
    m_origin = c - (c - m_origin) * factor;        // масштабирование относительно курсора
    m_scale *= factor;
    update();
}

void CanvasWidget::mousePressEvent(QMouseEvent* e) { m_lastMouse = e->pos(); setCursor(Qt::ClosedHandCursor); }

void CanvasWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (e->buttons() & Qt::LeftButton) {
        m_origin += QPointF(e->pos() - m_lastMouse);
        m_lastMouse = e->pos();
        update();
    }
}
