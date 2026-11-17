#pragma once
// Базовые геометрические типы (математические координаты: ось Y направлена ВВЕРХ).
#include <QVector>

struct Pt { double x = 0, y = 0; };

struct Seg { Pt a, b; };

// Прямоугольное отсекающее окно: (xmin,ymin) - нижний левый, (xmax,ymax) - верхний правый угол.
struct Rect {
    double xmin = 0, ymin = 0, xmax = 0, ymax = 0;
    // Окно как выпуклый многоугольник, вершины против часовой стрелки
    QVector<Pt> toPolygon() const {
        return { {xmin, ymin}, {xmax, ymin}, {xmax, ymax}, {xmin, ymax} };
    }
};
