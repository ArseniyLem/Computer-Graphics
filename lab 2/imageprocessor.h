// ============================================================================
//  imageprocessor.h — Вариант №10
//  Глобальная пороговая обработка + фильтры порядковых статистик.
//
//  Модуль НЕ зависит от GUI. Все алгоритмы реализованы вручную, без OpenCV.
//  Qt используется только как контейнер пикселей (QImage).
// ============================================================================
#pragma once

#include <QImage>

#include <array>
#include <cstdint>

namespace ImageProcessor {

/// Гистограмма яркости: hist[i] — число пикселей с яркостью i.
using Histogram = std::array<std::uint32_t, 256>;

/// Тип фильтра порядковых статистик.
enum class OrderFilter {
    Median,    ///< медиана окна (подавляет импульсный шум «соль и перец»)
    Min,       ///< минимум окна (эрозия; убирает «соль», утолщает «перец»)
    Max,       ///< максимум окна (дилатация; убирает «перец», утолщает «соль»)
    Midpoint   ///< (min + max) / 2 (хорош для гауссова/равномерного шума)
};

/// Форма структурирующего элемента (апертуры).
enum class KernelShape {
    Square,    ///< полный квадрат k×k
    Cross      ///< «крест»: центральная строка + центральный столбец (4r+1 точек)
};

/// Способ обработки границ изображения.
enum class BorderMode {
    Replicate, ///< Clamp to Edge — повтор крайних пикселей
    Reflect,   ///< зеркальное отражение (с дублированием крайнего пикселя)
    Zero       ///< Zero Padding — за границей считаем 0
};

/// Преобразование в Grayscale8 (BT.601: Y = 0.299R + 0.587G + 0.114B).
QImage toGrayscale(const QImage& src);

/// Гистограмма яркости (изображение при необходимости приводится к Grayscale8).
Histogram computeHistogram(const QImage& img);

/// Базовый итеративный алгоритм глобального порога (Gonzalez & Woods):
///  1) T0 = средняя яркость;  2) делим пиксели на G1 (<=T) и G2 (>T);
///  3) T = (mean(G1) + mean(G2)) / 2;  4) повторяем, пока |ΔT| >= epsilon.
int iterativeThreshold(const Histogram& hist, double epsilon = 0.5);

/// Метод Оцу: порог, максимизирующий межклассовую дисперсию
///  σ_B²(t) = w0(t) · w1(t) · (μ0(t) − μ1(t))².
/// Если максимум достигается на плато (пустой промежуток между модами),
/// возвращается середина плато.
int otsuThreshold(const Histogram& hist);

/// Бинаризация: яркость > threshold → 255, иначе 0. Результат — Grayscale8.
QImage applyThreshold(const QImage& src, int threshold);

/// Фильтр порядковых статистик. Grayscale8 обрабатывается как есть,
/// цветные изображения — независимо по каналам R, G, B (результат RGB32).
/// kernelSize — нечётное 3..15 (значения вне диапазона корректируются).
QImage orderStatisticFilter(const QImage& src,
                            OrderFilter filter,
                            int kernelSize,
                            KernelShape shape,
                            BorderMode border);

} // namespace ImageProcessor
