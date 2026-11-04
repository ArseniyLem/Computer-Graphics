#pragma once

#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVector>

/**
 * Модуль базовых растровых алгоритмов.
 *
 * Класс не зависит от GUI: каждая функция принимает целочисленные координаты
 * узлов пиксельной сетки и возвращает Result - список пикселей, пошаговый лог
 * и время выполнения.
 *
 * Соглашение о координатах: пиксель (i, j) - это квадрат единичного размера,
 * центр которого лежит в целочисленном узле сетки (i, j).
 * Ось X направлена вправо, ось Y - вверх (математическая ориентация).
 */
class RasterAlgorithms
{
public:
    enum class Algorithm {
        StepByStep,      ///< Пошаговый алгоритм
        DDA,             ///< ЦДА
        BresenhamLine,   ///< Брезенхем для отрезка
        BresenhamCircle, ///< Брезенхем для окружности
        CastlePitteway   ///< Кастл-Питвей
    };

    /// Параметры построения. Для отрезка - (x1,y1)-(x2,y2); для окружности - центр (x1,y1) и радиус.
    struct Params {
        int x1 = 0, y1 = 0;
        int x2 = 0, y2 = 0;
        int radius = 0;
    };

    /// Один "шаг" алгоритма для пошаговой визуализации: после него на холсте
    /// должно быть pixelEnd пикселей, а в лог относятся строки [logBegin, logEnd).
    /// Для отрезков шаг = один пиксель, для окружности - одна итерация цикла
    /// (до 8 симметричных пикселей).
    struct Step {
        int pixelEnd = 0;
        int logBegin = 0;
        int logEnd = 0;
    };

    struct Result {
        QVector<QPoint> pixels;  ///< растровые точки в порядке построения
        QStringList log;         ///< промежуточные значения шагов алгоритма
        QVector<Step> steps;     ///< границы шагов (заполняется только при withLog = true)
        int curLogBegin = 0;

        /// Начало шага: запоминаем позицию в логе. Строки лога до первого шага - "заголовок".
        void beginStep() { curLogBegin = log.size(); }
        /// Конец шага: фиксируем, сколько пикселей и строк лога ему соответствует.
        void endStep() { steps.push_back(Step{int(pixels.size()), curLogBegin, int(log.size())}); }

        double timeUs = 0.0;     ///< время одного вызова, мкс (high_resolution_clock)
    };

    static QString name(Algorithm a);
    static bool isCircle(Algorithm a) { return a == Algorithm::BresenhamCircle; }

    /// Универсальный вызов. withLog = false отключает формирование текстового лога
    /// (нужно для честного измерения времени).
    static Result run(Algorithm a, const Params &p, bool withLog);

    /// Среднее время одного вызова алгоритма (без лога) за `repeats` повторов, мкс.
    static double averageTimeUs(Algorithm a, const Params &p, int repeats);

    // --- Отдельные алгоритмы ---
    static Result stepByStep(int x1, int y1, int x2, int y2, bool withLog);
    static Result dda(int x1, int y1, int x2, int y2, bool withLog);
    static Result bresenhamLine(int x1, int y1, int x2, int y2, bool withLog);
    static Result bresenhamCircle(int cx, int cy, int r, bool withLog);
    static Result castlePitteway(int x1, int y1, int x2, int y2, bool withLog);
};
