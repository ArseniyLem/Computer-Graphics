// ============================================================================
//  imageprocessor.cpp — реализация алгоритмов варианта №10
// ============================================================================
#include "imageprocessor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ImageProcessor {

// ----------------------------------------------------------------------------
//  Вспомогательные функции (видны только в этом файле)
// ----------------------------------------------------------------------------
namespace {

/// Яркость пикселя по BT.601, целочисленная арифметика с округлением.
inline std::uint8_t luminance(QRgb p)
{
    return static_cast<std::uint8_t>(
        (299 * qRed(p) + 587 * qGreen(p) + 114 * qBlue(p) + 500) / 1000);
}

/// Индекс отражения: -1 -> 0, -2 -> 1, n -> n-1, n+1 -> n-2 ...
inline int reflectIndex(int i, int n)
{
    if (n == 1)
        return 0;
    while (i < 0 || i >= n) {
        if (i < 0)  i = -i - 1;
        if (i >= n) i = 2 * n - i - 1;
    }
    return i;
}

/// Отображает координату в «расширенной» области на координату исходного
/// изображения согласно режиму границ. Возвращает -1, если нужен ноль.
inline int mapIndex(int i, int n, BorderMode mode)
{
    if (i >= 0 && i < n)
        return i;
    switch (mode) {
    case BorderMode::Replicate: return std::clamp(i, 0, n - 1);
    case BorderMode::Reflect:   return reflectIndex(i, n);
    case BorderMode::Zero:      return -1;
    }
    return -1;
}

/// Строит дополненную (padded) копию канала размером (w+2r)×(h+2r).
/// После этого внутренний цикл фильтра не содержит проверок границ.
std::vector<std::uint8_t> padPlane(const std::vector<std::uint8_t>& src,
                                   int w, int h, int r, BorderMode mode)
{
    const int pw = w + 2 * r;
    const int ph = h + 2 * r;
    std::vector<std::uint8_t> pad(static_cast<std::size_t>(pw) * ph, 0);

    // Карта столбцов считается один раз и переиспользуется для всех строк.
    std::vector<int> colMap(static_cast<std::size_t>(pw));
    for (int px = 0; px < pw; ++px)
        colMap[px] = mapIndex(px - r, w, mode);

    for (int py = 0; py < ph; ++py) {
        const int sy = mapIndex(py - r, h, mode);
        if (sy < 0)
            continue;                                   // строка нулей
        const std::uint8_t* srow = &src[static_cast<std::size_t>(sy) * w];
        std::uint8_t* drow = &pad[static_cast<std::size_t>(py) * pw];
        for (int px = 0; px < pw; ++px)
            drow[px] = (colMap[px] < 0) ? 0 : srow[colMap[px]];
    }
    return pad;
}

/// Смещения элементов апертуры относительно центрального пикселя
/// в индексах дополненного канала (stride = pw).
std::vector<int> buildOffsets(int r, int pw, KernelShape shape)
{
    std::vector<int> offs;
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (shape == KernelShape::Cross && dx != 0 && dy != 0)
                continue;
            offs.push_back(dy * pw + dx);
        }
    }
    return offs;
}

/// Фильтрация одного канала (8 бит на пиксель).
std::vector<std::uint8_t> filterPlane(const std::vector<std::uint8_t>& plane,
                                      int w, int h, int r,
                                      OrderFilter filter,
                                      KernelShape shape,
                                      BorderMode border)
{
    const int pw = w + 2 * r;
    const std::vector<std::uint8_t> pad = padPlane(plane, w, h, r, border);
    const std::vector<int> offs = buildOffsets(r, pw, shape);
    const std::size_t n = offs.size();

    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h);
    std::vector<std::uint8_t> window(n);                // буфер значений окна

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // Указатель на центральный пиксель в дополненном канале.
            const std::uint8_t* c = &pad[static_cast<std::size_t>(y + r) * pw + (x + r)];
            std::uint8_t result = 0;

            switch (filter) {
            case OrderFilter::Median: {
                for (std::size_t k = 0; k < n; ++k)
                    window[k] = c[offs[k]];
                // nth_element: частичная сортировка за O(n) — на позицию n/2
                // попадает элемент, который стоял бы там в отсортированном окне.
                auto mid = window.begin() + static_cast<std::ptrdiff_t>(n / 2);
                std::nth_element(window.begin(), mid, window.end());
                result = *mid;
                break;
            }
            case OrderFilter::Min: {
                std::uint8_t mn = 255;
                for (std::size_t k = 0; k < n; ++k)
                    mn = std::min(mn, c[offs[k]]);
                result = mn;
                break;
            }
            case OrderFilter::Max: {
                std::uint8_t mx = 0;
                for (std::size_t k = 0; k < n; ++k)
                    mx = std::max(mx, c[offs[k]]);
                result = mx;
                break;
            }
            case OrderFilter::Midpoint: {
                std::uint8_t mn = 255, mx = 0;
                for (std::size_t k = 0; k < n; ++k) {
                    const std::uint8_t v = c[offs[k]];
                    mn = std::min(mn, v);
                    mx = std::max(mx, v);
                }
                result = static_cast<std::uint8_t>((static_cast<int>(mn) + mx) / 2);
                break;
            }
            }
            out[static_cast<std::size_t>(y) * w + x] = result;
        }
    }
    return out;
}

} // namespace

// ----------------------------------------------------------------------------
//  Градации серого и гистограмма
// ----------------------------------------------------------------------------
QImage toGrayscale(const QImage& src)
{
    if (src.isNull())
        return {};
    if (src.format() == QImage::Format_Grayscale8)
        return src;                                     // неявное разделение данных, копии нет

    const QImage argb = src.convertToFormat(QImage::Format_ARGB32);
    QImage dst(argb.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < argb.height(); ++y) {
        const auto* in = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        std::uint8_t* out = dst.scanLine(y);
        for (int x = 0; x < argb.width(); ++x)
            out[x] = luminance(in[x]);
    }
    return dst;
}

Histogram computeHistogram(const QImage& img)
{
    Histogram hist{};                                   // нули
    const QImage gray = toGrayscale(img);
    for (int y = 0; y < gray.height(); ++y) {
        const std::uint8_t* line = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x)
            ++hist[line[x]];
    }
    return hist;
}

// ----------------------------------------------------------------------------
//  Глобальная пороговая обработка
// ----------------------------------------------------------------------------
int iterativeThreshold(const Histogram& hist, double epsilon)
{
    double total = 0.0, sum = 0.0;
    for (int i = 0; i < 256; ++i) {
        total += hist[i];
        sum   += static_cast<double>(i) * hist[i];
    }
    if (total == 0.0)
        return 128;

    double T = sum / total;                             // T0 — средняя яркость
    for (int iter = 0; iter < 256; ++iter) {            // страховка от зацикливания
        // Яркости целые, поэтому условие i <= T эквивалентно i <= floor(T).
        const int t = static_cast<int>(std::floor(T));
        double n1 = 0, s1 = 0, n2 = 0, s2 = 0;
        for (int i = 0; i < 256; ++i) {
            if (i <= t) { n1 += hist[i]; s1 += static_cast<double>(i) * hist[i]; }
            else        { n2 += hist[i]; s2 += static_cast<double>(i) * hist[i]; }
        }
        if (n1 == 0.0 || n2 == 0.0)                     // один из классов пуст
            break;
        const double Tnew = 0.5 * (s1 / n1 + s2 / n2);
        const bool converged = std::abs(Tnew - T) < epsilon;
        T = Tnew;
        if (converged)
            break;
    }
    return std::clamp(static_cast<int>(std::lround(T)), 0, 255);
}

int otsuThreshold(const Histogram& hist)
{
    double total = 0.0, sumAll = 0.0;
    for (int i = 0; i < 256; ++i) {
        total  += hist[i];
        sumAll += static_cast<double>(i) * hist[i];
    }
    if (total == 0.0)
        return 128;

    double wB = 0.0, sumB = 0.0;                        // вес и сумма «фона» (<= t)
    double bestVar = -1.0;
    int first = 0, last = 0;

    for (int t = 0; t < 256; ++t) {
        wB += hist[t];
        if (wB == 0.0)
            continue;
        const double wF = total - wB;                   // вес «объекта» (> t)
        if (wF == 0.0)
            break;
        sumB += static_cast<double>(t) * hist[t];

        const double mB = sumB / wB;
        const double mF = (sumAll - sumB) / wF;
        const double var = wB * wF * (mB - mF) * (mB - mF);   // межклассовая дисперсия

        if (var > bestVar) { bestVar = var; first = last = t; }
        else if (var == bestVar) { last = t; }          // плато: расширяем интервал
    }
    return (first + last) / 2;
}

QImage applyThreshold(const QImage& src, int threshold)
{
    const QImage gray = toGrayscale(src);
    if (gray.isNull())
        return {};

    // Таблица подстановки: один вызов сравнения на градацию, а не на пиксель.
    std::array<std::uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i)
        lut[i] = (i > threshold) ? 255 : 0;

    QImage dst(gray.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < gray.height(); ++y) {
        const std::uint8_t* in = gray.constScanLine(y);
        std::uint8_t* out = dst.scanLine(y);
        for (int x = 0; x < gray.width(); ++x)
            out[x] = lut[in[x]];
    }
    return dst;
}

// ----------------------------------------------------------------------------
//  Фильтры порядковых статистик
// ----------------------------------------------------------------------------
QImage orderStatisticFilter(const QImage& src,
                            OrderFilter filter,
                            int kernelSize,
                            KernelShape shape,
                            BorderMode border)
{
    if (src.isNull())
        return {};

    int k = std::clamp(kernelSize, 3, 15);
    if (k % 2 == 0)
        ++k;
    const int r = k / 2;

    const bool gray = (src.format() == QImage::Format_Grayscale8);
    const QImage in = gray ? src : src.convertToFormat(QImage::Format_RGB32);
    const int w = in.width();
    const int h = in.height();
    const int channels = gray ? 1 : 3;

    // 1. Разбиваем изображение на планарные каналы.
    std::vector<std::vector<std::uint8_t>> planes(
        channels, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * h));
    for (int y = 0; y < h; ++y) {
        if (gray) {
            const std::uint8_t* line = in.constScanLine(y);
            std::copy(line, line + w, planes[0].begin() + static_cast<std::ptrdiff_t>(y) * w);
        } else {
            const auto* line = reinterpret_cast<const QRgb*>(in.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const std::size_t idx = static_cast<std::size_t>(y) * w + x;
                planes[0][idx] = static_cast<std::uint8_t>(qRed(line[x]));
                planes[1][idx] = static_cast<std::uint8_t>(qGreen(line[x]));
                planes[2][idx] = static_cast<std::uint8_t>(qBlue(line[x]));
            }
        }
    }

    // 2. Фильтруем каждый канал независимо.
    for (auto& p : planes)
        p = filterPlane(p, w, h, r, filter, shape, border);

    // 3. Собираем результат.
    QImage dst(w, h, gray ? QImage::Format_Grayscale8 : QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        if (gray) {
            std::uint8_t* line = dst.scanLine(y);
            std::copy_n(planes[0].begin() + static_cast<std::ptrdiff_t>(y) * w, w, line);
        } else {
            auto* line = reinterpret_cast<QRgb*>(dst.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const std::size_t idx = static_cast<std::size_t>(y) * w + x;
                line[x] = qRgb(planes[0][idx], planes[1][idx], planes[2][idx]);
            }
        }
    }
    return dst;
}

} // namespace ImageProcessor
