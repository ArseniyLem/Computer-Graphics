#include "MainWindow.h"
#include "CanvasWidget.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QRadioButton>
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QRegularExpression>
#include <algorithm>

MainWindow::MainWindow()
{
    setWindowTitle("Лабораторная работа 4, вариант 10: отсечение отрезков");
    resize(1200, 720);

    m_canvas = new CanvasWidget;
    m_log = new QPlainTextEdit; m_log->setReadOnly(true);
    m_log->setFont(QFont("Monospace", 9));

    m_rb1 = new QRadioButton("Часть 1: Сазерленд-Коэн");
    m_rb2 = new QRadioButton("Часть 2: Кирус-Бек");
    m_rb1->setChecked(true);

    m_polyCombo = new QComboBox;
    m_polyCombo->addItem("Многоугольник: шестиугольник (демо)");
    m_polyCombo->addItem("Многоугольник: прямоугольное окно из файла");

    auto* btnLoad  = new QPushButton("Загрузить входной файл...");
    auto* btnDemo  = new QPushButton("Демонстрационные данные");
    auto* btnReset = new QPushButton("Сбросить вид");

    auto* side = new QVBoxLayout;
    side->addWidget(m_rb1);
    side->addWidget(m_rb2);
    side->addWidget(m_polyCombo);
    side->addWidget(btnLoad);
    side->addWidget(btnDemo);
    side->addWidget(btnReset);
    side->addWidget(new QLabel("Журнал алгоритма:"));
    side->addWidget(m_log, 1);

    auto* sideW = new QWidget; sideW->setLayout(side); sideW->setFixedWidth(380);
    auto* lay = new QHBoxLayout(this);
    lay->addWidget(m_canvas, 1);
    lay->addWidget(sideW);

    connect(m_canvas, &CanvasWidget::logChanged, m_log, &QPlainTextEdit::setPlainText);
    connect(m_rb1, &QRadioButton::toggled, this, &MainWindow::onModeChanged);
    connect(m_polyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]{ applyData(); });
    connect(btnLoad,  &QPushButton::clicked, this, &MainWindow::loadFile);
    connect(btnDemo,  &QPushButton::clicked, this, [this]{ loadDemo(); });
    connect(btnReset, &QPushButton::clicked, m_canvas, &CanvasWidget::resetView);

    loadDemo();
}

// Демонстрационный набор: внутри, снаружи, пересечения под разными углами,
// вертикальный/горизонтальный отрезки, отрезок через два угла окна и др.
void MainWindow::loadDemo()
{
    m_rect = { -4, -3, 5, 4 };                                  // Xmin Ymin Xmax Ymax
    m_segs = {
        {{ 0,  0}, { 3,  2}},      // 1: целиком внутри
        {{-6,  5}, {-5, -1}},      // 2: целиком слева (тривиальный отказ)
        {{-7,  1}, { 2,  2}},      // 3: пересекает левую границу
        {{ 1, -5}, { 1,  6}},      // 4: вертикальный, пересекает верх и низ
        {{-6, -4}, { 7,  6}},      // 5: диагональ, обе точки снаружи
        {{-6,  2}, { 7,  2}},      // 6: горизонтальный, насквозь
        {{ 3, -5}, { 8,  0}},      // 7: срезает правый нижний угол
        {{ 6, -1}, { 6,  3}},      // 8: целиком справа
        {{-2,  5}, { 4,  5}},      // 9: целиком выше окна
        {{-5, -2}, { 1,  6}},      // 10: наклонный, пересекает левую и верхнюю границы
        {{-3, -1}, { 4,  3}},      // 11: внутри окна, но пересекает шестиугольник
    };
    // Выпуклый многоугольник для Части 2 (любой порядок обхода, нормализуется)
    m_demoPoly = { {-3, 0}, {-1.5, -3}, {2.5, -3}, {4.5, 0.5}, {2.5, 3.5}, {-1.5, 3.5} };
    m_polyCombo->blockSignals(true); m_polyCombo->setCurrentIndex(0); m_polyCombo->blockSignals(false);
    applyData();
}

// Формат файла: n; n строк X1 Y1 X2 Y2; строка Xmin Ymin Xmax Ymax.
// Текст между звёздочками (*...*) считается комментарием.
bool MainWindow::parseInput(const QString& textIn, QString& err)
{
    QString text = textIn;
    text.remove(QRegularExpression("\\*[^*]*\\*"));
    text.replace(',', '.');
    QStringList tok = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    bool ok = true;
    if (tok.isEmpty()) { err = "Файл пуст"; return false; }
    int n = tok[0].toInt(&ok);
    if (!ok || n < 0) { err = "Некорректное число отрезков"; return false; }
    if (tok.size() != 1 + 4 * n + 4) { err = QString("Ожидалось %1 чисел, найдено %2").arg(1 + 4 * n + 4).arg(tok.size()); return false; }
    QVector<double> v;
    for (int i = 1; i < tok.size(); ++i) { v << tok[i].toDouble(&ok); if (!ok) { err = "Не число: " + tok[i]; return false; } }
    QVector<Seg> segs;
    for (int i = 0; i < n; ++i) segs.push_back({{v[4*i], v[4*i+1]}, {v[4*i+2], v[4*i+3]}});
    Rect r { v[4*n], v[4*n+1], v[4*n+2], v[4*n+3] };
    if (r.xmin > r.xmax) std::swap(r.xmin, r.xmax);
    if (r.ymin > r.ymax) std::swap(r.ymin, r.ymax);
    m_segs = segs; m_rect = r;
    return true;
}

void MainWindow::loadFile()
{
    QString path = QFileDialog::getOpenFileName(this, "Входной файл", QString(), "Текст (*.txt);;Все файлы (*)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) { QMessageBox::warning(this, "Ошибка", "Не удалось открыть файл"); return; }
    QString err;
    if (!parseInput(QString::fromUtf8(f.readAll()), err)) { QMessageBox::warning(this, "Ошибка формата", err); return; }
    m_polyCombo->blockSignals(true); m_polyCombo->setCurrentIndex(1); m_polyCombo->blockSignals(false);
    applyData();
}

void MainWindow::applyData()
{
    QVector<Pt> poly = (m_polyCombo->currentIndex() == 0) ? m_demoPoly : m_rect.toPolygon();
    m_canvas->setData(m_segs, m_rect, poly);
    m_canvas->setMode(m_rb1->isChecked() ? CanvasWidget::Part1 : CanvasWidget::Part2);
}

void MainWindow::onModeChanged()
{
    m_canvas->setMode(m_rb1->isChecked() ? CanvasWidget::Part1 : CanvasWidget::Part2);
}
