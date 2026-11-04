#pragma once

#include <QMainWindow>

#include "rasteralgorithms.h"

class CanvasWidget;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QSpinBox;

/**
 * Главное окно: слева панель управления, справа холст и лог шагов.
 * Математика вынесена в RasterAlgorithms, отрисовка - в CanvasWidget.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onBuild();
    void onCompare();
    void onClear();
    void onHelp();
    void onNext();
    void onFinish();
    void onAlgorithmChanged();
    void onCursorCell(const QPoint &cell);

private:
    void buildUi();
    QSpinBox *makeSpin(int minV, int maxV, int value);
    RasterAlgorithms::Algorithm currentAlgorithm() const;
    RasterAlgorithms::Params currentParams() const;
    static int repeatsFor(int pixelCount);
    void showLog(const QStringList &lines);
    void applyStep(int k);        ///< нарисовать шаг k и дописать его строки в лог
    void finishStepping();        ///< показать итог и завершить пошаговый режим
    void updateStepControls();

    CanvasWidget *m_canvas = nullptr;
    QPlainTextEdit *m_log = nullptr;

    QComboBox *m_algoCombo = nullptr;
    QGroupBox *m_lineGroup = nullptr;
    QGroupBox *m_circleGroup = nullptr;
    QSpinBox *m_x1 = nullptr, *m_y1 = nullptr, *m_x2 = nullptr, *m_y2 = nullptr;
    QSpinBox *m_cx = nullptr, *m_cy = nullptr, *m_radius = nullptr;
    QCheckBox *m_showIdeal = nullptr;
    QCheckBox *m_autoFit = nullptr;

    // --- пошаговый режим ---
    QCheckBox *m_stepMode = nullptr;
    QPushButton *m_btnNext = nullptr;
    QPushButton *m_btnFinish = nullptr;
    QLabel *m_stepLabel = nullptr;
    RasterAlgorithms::Result m_result; ///< полный результат текущего построения
    int m_stepIndex = -1;              ///< индекс последнего показанного шага
    bool m_stepping = false;           ///< идёт ли сейчас пошаговое рисование
    QString m_timeLine;                ///< строка со временем, выводится в конце

    QLabel *m_singleTimeLabel = nullptr;
    QLabel *m_avgTimeLabel = nullptr;
    QLabel *m_countLabel = nullptr;
    QLabel *m_cursorLabel = nullptr;
};
