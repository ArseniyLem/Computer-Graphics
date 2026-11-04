#include "rasteralgorithms.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace {

using Clock = std::chrono::high_resolution_clock;

/// Время, прошедшее с t0, в микросекундах (с дробной частью).
inline double elapsedUs(Clock::time_point t0)
{
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

/// Округление "к ближайшему" (0.5 -> вверх).
inline int roundInt(double v) { return static_cast<int>(std::floor(v + 0.5)); }

inline int sign(int v) { return (v > 0) - (v < 0); }

inline QString num(double v, int prec = 3) { return QString::number(v, 'f', prec); }

/// Сокращает длинную строку для вывода в лог.
QString abbreviate(const QString &s, int maxLen = 60)
{
    if (s.size() <= maxLen)
        return s;
    return s.left(maxLen) + QString("... (длина %1)").arg(s.size());
}

} // namespace

QString RasterAlgorithms::name(Algorithm a)
{
    switch (a) {
    case Algorithm::StepByStep:      return "Пошаговый алгоритм";
    case Algorithm::DDA:             return "Алгоритм ЦДА (DDA)";
    case Algorithm::BresenhamLine:   return "Алгоритм Брезенхема (отрезок)";
    case Algorithm::BresenhamCircle: return "Алгоритм Брезенхема (окружность)";
    case Algorithm::CastlePitteway:  return "Алгоритм Кастла-Питвея (отрезок)";
    }
    return QString();
}

RasterAlgorithms::Result RasterAlgorithms::run(Algorithm a, const Params &p, bool withLog)
{
    switch (a) {
    case Algorithm::StepByStep:      return stepByStep(p.x1, p.y1, p.x2, p.y2, withLog);
    case Algorithm::DDA:             return dda(p.x1, p.y1, p.x2, p.y2, withLog);
    case Algorithm::BresenhamLine:   return bresenhamLine(p.x1, p.y1, p.x2, p.y2, withLog);
    case Algorithm::BresenhamCircle: return bresenhamCircle(p.x1, p.y1, p.radius, withLog);
    case Algorithm::CastlePitteway:  return castlePitteway(p.x1, p.y1, p.x2, p.y2, withLog);
    }
    return Result();
}

double RasterAlgorithms::averageTimeUs(Algorithm a, const Params &p, int repeats)
{
    if (repeats < 1)
        repeats = 1;
    volatile std::size_t sink = 0; // не даём компилятору выбросить вызовы
    const auto t0 = Clock::now();
    for (int i = 0; i < repeats; ++i) {
        Result r = run(a, p, false);
        sink = sink + static_cast<std::size_t>(r.pixels.size());
    }
    return elapsedUs(t0) / repeats;
}

// ============================================================================
// 1. Пошаговый алгоритм
// ----------------------------------------------------------------------------
// Уравнение прямой y = k*x + b. Если |dx| >= |dy|, идём по x с шагом 1 и
// на каждом шаге вычисляем y и округляем. Иначе роли осей меняются
// (x = k*y + b, идём по y). На каждом шаге - умножение и округление.
// ============================================================================
RasterAlgorithms::Result RasterAlgorithms::stepByStep(int x1, int y1, int x2, int y2, bool withLog)
{
    Result r;
    const auto t0 = Clock::now();
    const int dx = x2 - x1, dy = y2 - y1;

    if (withLog)
        r.log << QString("=== Пошаговый алгоритм: (%1; %2) -> (%3; %4), dx=%5, dy=%6 ===")
                     .arg(x1).arg(y1).arg(x2).arg(y2).arg(dx).arg(dy);

    if (dx == 0 && dy == 0) {
        if (withLog) r.beginStep();
        r.pixels.push_back(QPoint(x1, y1));
        if (withLog) {
            r.log << "Вырожденный отрезок: один пиксель.";
            r.endStep();
        }
    } else if (std::abs(dx) >= std::abs(dy)) {
        const double k = double(dy) / dx;
        const double b = y1 - k * x1;
        const int step = sign(dx);
        r.pixels.reserve(std::abs(dx) + 1);
        if (withLog)
            r.log << QString("Идём по X. k = dy/dx = %1, b = y1 - k*x1 = %2").arg(num(k, 5)).arg(num(b, 5));
        for (int x = x1;; x += step) {
            const double y = k * x + b;
            const int yi = roundInt(y);
            if (withLog) r.beginStep();
            r.pixels.push_back(QPoint(x, yi));
            if (withLog) {
                r.log << QString("x=%1  y=k*x+b=%2  round -> y=%3  => пиксель (%1; %3)")
                             .arg(x).arg(num(y, 4)).arg(yi);
                r.endStep();
            }
            if (x == x2)
                break;
        }
    } else {
        const double k = double(dx) / dy;
        const double b = x1 - k * y1;
        const int step = sign(dy);
        r.pixels.reserve(std::abs(dy) + 1);
        if (withLog)
            r.log << QString("Идём по Y. k = dx/dy = %1, b = x1 - k*y1 = %2").arg(num(k, 5)).arg(num(b, 5));
        for (int y = y1;; y += step) {
            const double x = k * y + b;
            const int xi = roundInt(x);
            if (withLog) r.beginStep();
            r.pixels.push_back(QPoint(xi, y));
            if (withLog) {
                r.log << QString("y=%1  x=k*y+b=%2  round -> x=%3  => пиксель (%3; %1)")
                             .arg(y).arg(num(x, 4)).arg(xi);
                r.endStep();
            }
            if (y == y2)
                break;
        }
    }

    r.timeUs = elapsedUs(t0);
    return r;
}

// ============================================================================
// 2. Алгоритм ЦДА (DDA)
// ----------------------------------------------------------------------------
// L = max(|dx|,|dy|) - число шагов. Приращения xinc = dx/L, yinc = dy/L
// (одно из них равно +-1). На каждом шаге к вещественным x, y прибавляются
// приращения, а пиксель получается округлением. Умножений в цикле нет -
// только сложения.
// ============================================================================
RasterAlgorithms::Result RasterAlgorithms::dda(int x1, int y1, int x2, int y2, bool withLog)
{
    Result r;
    const auto t0 = Clock::now();
    const int dx = x2 - x1, dy = y2 - y1;
    const int L = std::max(std::abs(dx), std::abs(dy));

    if (withLog)
        r.log << QString("=== ЦДА: (%1; %2) -> (%3; %4), dx=%5, dy=%6 ===")
                     .arg(x1).arg(y1).arg(x2).arg(y2).arg(dx).arg(dy);

    if (L == 0) {
        if (withLog) r.beginStep();
        r.pixels.push_back(QPoint(x1, y1));
        if (withLog) {
            r.log << "Вырожденный отрезок: один пиксель.";
            r.endStep();
        }
    } else {
        const double xinc = double(dx) / L;
        const double yinc = double(dy) / L;
        double x = x1, y = y1;
        r.pixels.reserve(L + 1);
        if (withLog)
            r.log << QString("L = max(|dx|,|dy|) = %1, xinc = %2, yinc = %3")
                         .arg(L).arg(num(xinc, 5)).arg(num(yinc, 5));
        for (int i = 0; i <= L; ++i) {
            const int px = roundInt(x), py = roundInt(y);
            if (withLog) r.beginStep();
            r.pixels.push_back(QPoint(px, py));
            if (withLog) {
                r.log << QString("i=%1  x=%2  y=%3  => пиксель (%4; %5)")
                             .arg(i).arg(num(x, 4)).arg(num(y, 4)).arg(px).arg(py);
                r.endStep();
            }
            x += xinc;
            y += yinc;
        }
    }

    r.timeUs = elapsedUs(t0);
    return r;
}

// ============================================================================
// 3. Алгоритм Брезенхема для отрезка (целочисленный, все октанты)
// ----------------------------------------------------------------------------
// Переменная err = dx + dy (dy берётся со знаком минус) хранит удвоенную
// "ошибку" отклонения от идеальной прямой. На каждом шаге:
//   e2 = 2*err;  если e2 >= dy -> шаг по x;  если e2 <= dx -> шаг по y.
// Используются только целочисленные сложения и сдвиг.
// ============================================================================
RasterAlgorithms::Result RasterAlgorithms::bresenhamLine(int x1, int y1, int x2, int y2, bool withLog)
{
    Result r;
    const auto t0 = Clock::now();

    const int dx = std::abs(x2 - x1);
    const int dy = -std::abs(y2 - y1);
    const int sx = x1 < x2 ? 1 : -1;
    const int sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    int x = x1, y = y1;
    int i = 0;

    if (withLog)
        r.log << QString("=== Брезенхем (отрезок): (%1; %2) -> (%3; %4) ===").arg(x1).arg(y1).arg(x2).arg(y2)
              << QString("dx=|x2-x1|=%1, dy=-|y2-y1|=%2, sx=%3, sy=%4, err0=dx+dy=%5")
                     .arg(dx).arg(dy).arg(sx).arg(sy).arg(err);

    r.pixels.reserve(std::max(dx, -dy) + 1);
    for (;;) {
        if (withLog) r.beginStep();
        r.pixels.push_back(QPoint(x, y));
        const int e2 = 2 * err;
        if (withLog) {
            QString action;
            if (x == x2 && y == y2)
                action = "конец отрезка";
            else {
                if (e2 >= dy) action += QString("e2>=dy: err+=dy, x%1; ").arg(sx > 0 ? "++" : "--");
                if (e2 <= dx) action += QString("e2<=dx: err+=dx, y%1").arg(sy > 0 ? "++" : "--");
            }
            r.log << QString("i=%1  пиксель (%2; %3)  err=%4  e2=%5  %6")
                         .arg(i).arg(x).arg(y).arg(err).arg(e2).arg(action);
            r.endStep();
        }
        if (x == x2 && y == y2)
            break;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
        ++i;
    }

    r.timeUs = elapsedUs(t0);
    return r;
}

// ============================================================================
// 4. Алгоритм Брезенхема для окружности
// ----------------------------------------------------------------------------
// Строится один октант (x от 0 до y), остальные получаются симметрией:
// (+-x,+-y), (+-y,+-x) относительно центра. Решающая переменная d = 3 - 2r:
//   d < 0  -> y не меняется,  d += 4x + 6
//   d >= 0 -> y уменьшается,  d += 4(x - y) + 10
// ============================================================================
RasterAlgorithms::Result RasterAlgorithms::bresenhamCircle(int cx, int cy, int radius, bool withLog)
{
    Result r;
    const auto t0 = Clock::now();

    if (withLog)
        r.log << QString("=== Брезенхем (окружность): центр (%1; %2), R=%3 ===").arg(cx).arg(cy).arg(radius);

    if (radius < 0) {
        if (withLog)
            r.log << "Ошибка: радиус должен быть неотрицательным.";
        r.timeUs = elapsedUs(t0);
        return r;
    }

    int x = 0, y = radius;
    int d = 3 - 2 * radius;
    r.pixels.reserve(8 * (radius + 1));
    if (withLog)
        r.log << QString("Старт: x=0, y=R=%1, d0=3-2R=%2").arg(y).arg(d);

    while (y >= x) {
        if (withLog) r.beginStep();
        // Восемь симметричных точек; дубликаты (на осях и диагоналях) отбрасываем.
        const QPoint pts[8] = {
            {cx + x, cy + y}, {cx - x, cy + y}, {cx + x, cy - y}, {cx - x, cy - y},
            {cx + y, cy + x}, {cx - y, cy + x}, {cx + y, cy - x}, {cx - y, cy - x}
        };
        for (int i = 0; i < 8; ++i) {
            bool dup = false;
            for (int j = 0; j < i; ++j)
                if (pts[j] == pts[i]) { dup = true; break; }
            if (!dup)
                r.pixels.push_back(pts[i]);
        }

        if (withLog) {
            QString action;
            if (d < 0)
                action = QString("d<0: d = d+4x+6 = %1").arg(d + 4 * x + 6);
            else
                action = QString("d>=0: d = d+4(x-y)+10 = %1, y-- -> %2").arg(d + 4 * (x - y) + 10).arg(y - 1);
            r.log << QString("x=%1  y=%2  d=%3  октант: (%4; %5)  %6")
                         .arg(x).arg(y).arg(d).arg(cx + x).arg(cy + y).arg(action);
            r.endStep();
        }

        if (d < 0) {
            d += 4 * x + 6;
        } else {
            d += 4 * (x - y) + 10;
            --y;
        }
        ++x;
    }

    r.timeUs = elapsedUs(t0);
    return r;
}

// ============================================================================
// 5. Алгоритм Кастла-Питвея (Castle-Pitteway)
// ----------------------------------------------------------------------------
// Идея: отрезок после приведения к первому октанту (major >= minor >= 0) -
// это строка из двух символов:
//   S - шаг только по главной оси,
//   D - диагональный шаг (по главной и по вспомогательной оси).
// Всего major шагов, из них D ровно minor раз, S - major - minor раз.
// Строка строится аналогом алгоритма Евклида над счётчиками m1 (S) и m2 (D):
//   пока m1 != m2:
//       если m1 > m2: m1 -= m2; s2 = s1 + reverse(s2)
//       иначе:        m2 -= m1; s1 = s2 + reverse(s1)
//   итоговая строка = (s2 + reverse(s1)) повторить m1 раз.
// Никаких вычислений с координатами и ошибкой в самом построении нет -
// только операции над строками; затем строка один раз "проигрывается".
// ============================================================================
RasterAlgorithms::Result RasterAlgorithms::castlePitteway(int x1, int y1, int x2, int y2, bool withLog)
{
    Result r;
    const auto t0 = Clock::now();

    const int dx = x2 - x1, dy = y2 - y1;
    const int adx = std::abs(dx), ady = std::abs(dy);

    if (withLog)
        r.log << QString("=== Кастл-Питвей: (%1; %2) -> (%3; %4), dx=%5, dy=%6 ===")
                     .arg(x1).arg(y1).arg(x2).arg(y2).arg(dx).arg(dy);

    if (adx == 0 && ady == 0) {
        if (withLog) r.beginStep();
        r.pixels.push_back(QPoint(x1, y1));
        if (withLog) {
            r.log << "Вырожденный отрезок: один пиксель.";
            r.endStep();
        }
        r.timeUs = elapsedUs(t0);
        return r;
    }

    // Приведение к первому октанту: главная ось - та, где смещение больше.
    const bool steep = ady > adx;
    const int major = steep ? ady : adx;
    const int minor = steep ? adx : ady;
    const int majorSign = steep ? sign(dy) : sign(dx);
    const int minorSign = steep ? (dx == 0 ? 1 : sign(dx)) : (dy == 0 ? 1 : sign(dy));

    int m1 = major - minor; // число шагов S
    int m2 = minor;         // число шагов D
    if (withLog)
        r.log << QString("Главная ось: %1. Всего шагов=%2, S (по оси)=%3, D (диагональ)=%4")
                     .arg(steep ? "Y" : "X").arg(major).arg(m1).arg(m2);

    std::string s1 = "S", s2 = "D", word;

    if (m1 == 0) {
        word.assign(m2, 'D');
    } else if (m2 == 0) {
        word.assign(m1, 'S');
    } else {
        int iter = 0;
        while (m1 != m2) {
            ++iter;
            if (m1 > m2) {
                m1 -= m2;
                std::string rev(s2.rbegin(), s2.rend());
                s2 = s1 + rev;
                if (withLog)
                    r.log << QString("Евклид #%1: m1>m2 -> m1=%2, s2=s1+rev(s2)=\"%3\"")
                                 .arg(iter).arg(m1).arg(abbreviate(QString::fromStdString(s2)));
            } else {
                m2 -= m1;
                std::string rev(s1.rbegin(), s1.rend());
                s1 = s2 + rev;
                if (withLog)
                    r.log << QString("Евклид #%1: m1<m2 -> m2=%2, s1=s2+rev(s1)=\"%3\"")
                                 .arg(iter).arg(m2).arg(abbreviate(QString::fromStdString(s1)));
            }
        }
        std::string block = s2 + std::string(s1.rbegin(), s1.rend());
        word.reserve(block.size() * m1);
        for (int i = 0; i < m1; ++i)
            word += block;
        if (withLog)
            r.log << QString("m1=m2=%1: строка = (s2+rev(s1)) x %1").arg(m1);
    }

    if (withLog)
        r.log << QString("Итоговая строка шагов: \"%1\" (S=по оси, D=диагональ)")
                     .arg(abbreviate(QString::fromStdString(word), 120));

    // Проигрываем строку: пиксель за пикселем от (x1, y1).
    int x = x1, y = y1;
    r.pixels.reserve(static_cast<int>(word.size()) + 1);
    if (withLog) r.beginStep();
    r.pixels.push_back(QPoint(x, y));
    if (withLog) {
        r.log << QString("Старт: пиксель (%1; %2)").arg(x).arg(y);
        r.endStep();
    }

    int i = 0;
    for (char c : word) {
        if (withLog) r.beginStep();
        ++i;
        // шаг по главной оси
        if (steep) y += majorSign; else x += majorSign;
        // диагональный шаг дополнительно двигает вспомогательную ось
        if (c == 'D') {
            if (steep) x += minorSign; else y += minorSign;
        }
        r.pixels.push_back(QPoint(x, y));
        if (withLog) {
            r.log << QString("шаг %1: '%2' => пиксель (%3; %4)").arg(i).arg(c).arg(x).arg(y);
            r.endStep();
        }
    }

    r.timeUs = elapsedUs(t0);
    return r;
}
