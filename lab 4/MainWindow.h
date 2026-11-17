#pragma once
#include <QWidget>
#include <QVector>
#include "Geometry.h"

class CanvasWidget;
class QPlainTextEdit;
class QComboBox;
class QRadioButton;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow();
private slots:
    void loadFile();
    void applyData();
    void onModeChanged();
private:
    void loadDemo();
    bool parseInput(const QString& text, QString& err);

    CanvasWidget* m_canvas;
    QPlainTextEdit* m_log;
    QRadioButton* m_rb1;
    QRadioButton* m_rb2;
    QComboBox* m_polyCombo;

    QVector<Seg> m_segs;
    Rect m_rect;
    QVector<Pt> m_demoPoly;
};
