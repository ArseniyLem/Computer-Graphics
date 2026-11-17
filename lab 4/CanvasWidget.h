#pragma once
#include <QWidget>
#include <QVector>
#include <QPointF>
#include "Geometry.h"
#include "Clipping.h"

// Виджет-холст: сетка, оси, окно/многоугольник, отрезки и видимые части.
// Колесо мыши - масштаб, перетаскивание ЛКМ - сдвиг.
class CanvasWidget : public QWidget {
    Q_OBJECT
public:
    enum Mode { Part1 = 1, Part2 = 2 };
    explicit CanvasWidget(QWidget* parent = nullptr);

    void setData(const QVector<Seg>& segs, const Rect& window, const QVector<Pt>& polygon);
    void setMode(Mode m);
    void resetView() { m_needFit = true; update(); }

signals:
    void logChanged(const QString& text);

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    void recompute();
    void fitView();
    QPointF toScreen(const Pt& p) const { return { m_origin.x() + p.x * m_scale, m_origin.y() - p.y * m_scale }; }
    void drawGrid(QPainter& p);
    void drawClipRegion(QPainter& p);
    void drawSegments(QPainter& p);
    void drawLegend(QPainter& p);

    QVector<Seg> m_segs;
    Rect m_window;
    QVector<Pt> m_poly;               // CCW
    QVector<ClipResult> m_results;
    Mode m_mode = Part1;

    double m_scale = 30.0;            // пикселей на единицу
    QPointF m_origin { 0, 0 };        // экранное положение точки (0,0)
    bool m_needFit = true;
    QPoint m_lastMouse;
};
