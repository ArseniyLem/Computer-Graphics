#include "Clipping.h"
#include <algorithm>
#include <cmath>

static QString f(double v) { return QString::number(v, 'f', 3); }
static QString bin4(int c) { return QString::number(c, 2).rightJustified(4, '0'); }
static QString ps(const Pt& p) { return "(" + f(p.x) + "; " + f(p.y) + ")"; }

// ============================ Сазерленд-Коэн ================================

// Характеристический код точки:
//   b0 = 1, если y > ymax;  b1 = 1, если y < ymin;
//   b2 = 1, если x > xmax;  b3 = 1, если x < xmin.
// Код 0000 - точка внутри окна (или на границе).
int CohenSutherland::outcode(const Pt& p, const Rect& w)
{
    int c = 0;
    if (p.y > w.ymax) c |= TOP;
    if (p.y < w.ymin) c |= BOTTOM;
    if (p.x > w.xmax) c |= RIGHT;
    if (p.x < w.xmin) c |= LEFT;
    return c;
}

ClipResult CohenSutherland::clip(const Seg& s, const Rect& w)
{
    ClipResult r;
    Pt p1 = s.a, p2 = s.b;

    for (int iter = 0; iter < 10; ++iter) {          // защита от зацикливания
        int o1 = outcode(p1, w), o2 = outcode(p2, w);
        r.log += QString("  O1=%1, O2=%2\n").arg(bin4(o1), bin4(o2));

        // Случай 1: оба кода 0000 -> отрезок целиком внутри, принимаем.
        if ((o1 | o2) == 0) {
            r.visible = true; r.seg = {p1, p2};
            r.log += "  O1|O2 = 0000 -> отрезок полностью виден\n";
            return r;
        }
        // Случай 2: O1 & O2 != 0 -> обе точки по одну сторону от одной границы,
        // отрезок целиком невидим.
        if ((o1 & o2) != 0) {
            r.visible = false;
            r.log += "  O1&O2 != 0000 -> отрезок полностью невидим\n";
            return r;
        }
        // Случаи 3-4: нужно найти пересечение с границей окна.
        // Шаг 1: P1 должна лежать вне окна, иначе меняем P1 и P2 местами.
        if (o1 == 0) { std::swap(p1, p2); std::swap(o1, o2); }

        // Явное уравнение прямой: y = k(x - x1) + y1, k = dy/dx
        double dx = p2.x - p1.x, dy = p2.y - p1.y;
        double k  = (dx != 0.0) ? dy / dx : 0.0;     // для вертикальной прямой k не определён
        Pt np = p1;

        // Шаг 2: по единичному биту кода P1 определяем границу
        if (o1 & TOP) {                 // y = ymax:  x = x1 + (1/k)(ymax - y1)
            np.y = w.ymax;
            np.x = (dx == 0.0) ? p1.x : p1.x + (w.ymax - p1.y) / k;
            r.log += "  P1 выше окна: пересечение с y=ymax, x = x1 + (1/k)(ymax-y1)";
        } else if (o1 & BOTTOM) {       // y = ymin:  x = x1 + (1/k)(ymin - y1)
            np.y = w.ymin;
            np.x = (dx == 0.0) ? p1.x : p1.x + (w.ymin - p1.y) / k;
            r.log += "  P1 ниже окна: пересечение с y=ymin, x = x1 + (1/k)(ymin-y1)";
        } else if (o1 & RIGHT) {        // x = xmax:  y = y1 + k(xmax - x1)
            np.x = w.xmax;
            np.y = (dy == 0.0) ? p1.y : p1.y + k * (w.xmax - p1.x);
            r.log += "  P1 правее окна: пересечение с x=xmax, y = y1 + k(xmax-x1)";
        } else {                        // LEFT, x = xmin:  y = y1 + k(xmin - x1)
            np.x = w.xmin;
            np.y = (dy == 0.0) ? p1.y : p1.y + k * (w.xmin - p1.x);
            r.log += "  P1 левее окна: пересечение с x=xmin, y = y1 + k(xmin-x1)";
        }
        r.log += QString(", k=%1\n  P1* = %2\n").arg(dx != 0.0 ? f(k) : QString("inf"), ps(np));

        // Шаг 4: P1P2 => P1*P2, далее повторяем с шага 1 (проверка тривиальных случаев)
        p1 = np;
    }
    r.visible = false;
    return r;
}

// ============================== Кирус-Бек ===================================

QVector<Pt> CyrusBeck::normalizeCCW(QVector<Pt> poly)
{
    double area2 = 0;                       // удвоенная ориентированная площадь
    for (int i = 0; i < poly.size(); ++i) {
        const Pt& a = poly[i]; const Pt& b = poly[(i + 1) % poly.size()];
        area2 += a.x * b.y - b.x * a.y;
    }
    if (area2 < 0) std::reverse(poly.begin(), poly.end());   // был по часовой -> разворачиваем
    return poly;
}

// Отрезок:  P(t) = P1 + t*D,  D = P2 - P1,  0 <= t <= 1.
// Для ребра Ei = [E, F] (обход против часовой стрелки) ВНУТРЕННЯЯ нормаль  N = (-(Fy-Ey), Fx-Ex).
// Точка P(t) внутри полуплоскости ребра  <=>  N·(P(t) - E) >= 0.
// Обозначим  S = N·D,  Q = N·(P1 - E);  тогда  Q + t*S >= 0,  а точка пересечения:  t = -Q/S.
//   S > 0 : при росте t отрезок идёт в сторону нормали (ВХОД)   -> t попадает в Tвх,  t >= -Q/S
//   S < 0 : отрезок идёт против нормали             (ВЫХОД)  -> t попадает в Tвых, t <= -Q/S
//   S = 0 : отрезок параллелен ребру: Q < 0 - целиком снаружи, Q >= 0 - ребро не ограничивает.
ClipResult CyrusBeck::clip(const Seg& s, const QVector<Pt>& poly)
{
    ClipResult r;
    const double eps = 1e-12;
    Pt D { s.b.x - s.a.x, s.b.y - s.a.y };
    QVector<double> Tin, Tout;

    for (int i = 0; i < poly.size(); ++i) {
        const Pt& E = poly[i];
        const Pt& F = poly[(i + 1) % poly.size()];
        Pt N { -(F.y - E.y), (F.x - E.x) };                       // внутренняя нормаль
        double S = N.x * D.x + N.y * D.y;                          // N·(P2-P1)
        double Q = N.x * (s.a.x - E.x) + N.y * (s.a.y - E.y);      // N·(P1-E)

        if (std::fabs(S) < eps) {                                  // параллельно ребру
            r.log += QString("  E%1: S=0 (параллельно), Q=%2\n").arg(i + 1).arg(f(Q));
            if (Q < 0) {
                r.visible = false;
                r.log += "  отрезок снаружи относительно ребра -> невидим\n";
                return r;
            }
            continue;
        }
        double t = -Q / S;
        if (S > 0) { Tin.push_back(t);  r.log += QString("  E%1: S=%2 (вход),  t=%3\n").arg(i + 1).arg(f(S), f(t)); }
        else       { Tout.push_back(t); r.log += QString("  E%1: S=%2 (выход), t=%3\n").arg(i + 1).arg(f(S), f(t)); }
    }

    // Итог: tвх = max(Tвх), tвых = min(Tвых). Начальные границы 0 и 1 соответствуют P1 и P2:
    // пустой Tвх -> начало внутри (t=0), пустой Tвых -> конец внутри (t=1).
    // (Значения t вне [0;1] не выбрасываем, а обрезаем границами 0 и 1 - это даёт корректный
    //  результат и для t>1 у входящего ребра, и для t<0 у выходящего.)
    double tIn = 0.0, tOut = 1.0;
    for (double t : Tin)  tIn  = std::max(tIn, t);
    for (double t : Tout) tOut = std::min(tOut, t);
    r.log += QString("  tвх = %1, tвых = %2\n").arg(f(tIn), f(tOut));

    if (tIn > tOut) {                                              // пустое пересечение
        r.visible = false;
        r.log += "  tвх > tвых -> невидим\n";
        return r;
    }
    r.visible = true;
    r.seg = { { s.a.x + tIn  * D.x, s.a.y + tIn  * D.y },
              { s.a.x + tOut * D.x, s.a.y + tOut * D.y } };
    r.log += "  Pвх=" + ps(r.seg.a) + ", Pвых=" + ps(r.seg.b) + "\n";
    return r;
}
