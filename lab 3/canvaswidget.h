#pragma once

#include <QPoint>
#include <QPointF>
#include <QVector>
#include <QWidget>

/**
 * Холст: рисует пиксельную сетку, оси X/Y с подписями и растровые точки.
 *
 * Привязка к сетке: мировая координата (i, j) - целочисленный узел сетки.
 * Пиксель (i, j) рисуется квадратом со стороной в 1 единицу, центр которого
 * находится ровно в узле (i, j); линии сетки проходят через центры пикселей.
 *
 * Преобразование мир -> экран:
 *     sx = origin.x + X * scale
 *     sy = origin.y - Y * scale      (ось Y экрана направлена вниз)
 *
 * Колесо мыши - зум относительно курсора, перетаскивание ЛКМ - сдвиг.
 */
class CanvasWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CanvasWidget(QWidget *parent = nullptr);

    /// pixels - что рисовать сейчас. totalCount - сколько пикселей будет в готовой фигуре
    /// (нужно, чтобы при пошаговом рисовании не красить недорисованный хвост как "конец отрезка");
    /// -1 = totalCount равен pixels.size(). highlightFrom - индекс, начиная с которого пиксели
    /// подсвечиваются как "только что построенные" (-1 = без подсветки).
    void setPixels(const QVector<QPoint> &pixels, int totalCount = -1, int highlightFrom = -1);
    void setIdealLine(const QPoint &a, const QPoint &b);
    void setIdealCircle(const QPoint &center, int radius);
    void clearAll();
    void setShowIdeal(bool on);

    void resetView();   ///< начало координат в центре, масштаб по умолчанию
    void fitToShape();  ///< подогнать масштаб под построенную фигуру

signals:
    void cursorCellChanged(const QPoint &cell); ///< пиксель (узел) под курсором

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    enum class Shape { None, Line, Circle };

    QPointF toScreen(const QPointF &world) const;
    QPointF toWorld(const QPointF &screen) const;

    QVector<QPoint> m_pixels;
    Shape m_shape = Shape::None;
    QPoint m_a, m_b;      ///< концы идеального отрезка (или центр в m_a)
    int m_radius = 0;
    int m_totalPixels = -1;
    int m_highlightFrom = -1;
    bool m_showIdeal = true;

    double m_scale = 24.0;     ///< пикселей экрана на 1 единицу сетки
    QPointF m_origin;          ///< экранная позиция мировой точки (0, 0)
    bool m_viewInitialized = false;

    bool m_dragging = false;
    QPoint m_lastMouse;
};
