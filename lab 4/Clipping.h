#pragma once
#include "Geometry.h"
#include <QString>
#include <QVector>

// Результат отсечения одного отрезка
struct ClipResult {
    bool visible = false;   // есть ли видимая часть
    Seg  seg;               // видимая часть (имеет смысл, если visible == true)
    QString log;            // пошаговый протокол работы алгоритма (для окна журнала)
};

// ---------------------------------------------------------------------------
// ЧАСТЬ 1. Алгоритм Сазерленда-Коэна (пересечения - через явное уравнение прямой
// y = k(x - x1) + y1, k = (y2 - y1)/(x2 - x1)).
// ---------------------------------------------------------------------------
class CohenSutherland {
public:
    // Биты характеристического кода (b0 b1 b2 b3) = (сверху, снизу, справа, слева)
    enum : int { TOP = 8 /*b0: y>ymax*/, BOTTOM = 4 /*b1: y<ymin*/,
                 RIGHT = 2 /*b2: x>xmax*/, LEFT = 1 /*b3: x<xmin*/ };

    static int outcode(const Pt& p, const Rect& w);
    static ClipResult clip(const Seg& s, const Rect& w);
};

// ---------------------------------------------------------------------------
// ЧАСТЬ 2. Алгоритм Кируса-Бека (отсечение выпуклым многоугольником).
// ---------------------------------------------------------------------------
class CyrusBeck {
public:
    // Приводит вершины к обходу против часовой стрелки (знак площади по формуле шнурования)
    static QVector<Pt> normalizeCCW(QVector<Pt> poly);
    // poly - выпуклый многоугольник, вершины против часовой стрелки
    static ClipResult clip(const Seg& s, const QVector<Pt>& poly);
};
