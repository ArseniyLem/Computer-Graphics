#include "mainwindow.h"

#include "canvaswidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

#include <algorithm>

using RA = RasterAlgorithms;

namespace {
constexpr int kMaxLogLines = 3000; // чтобы GUI не "вис" на очень длинных отрезках
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Базовые растровые алгоритмы - лабораторная работа");
    resize(1200, 760);
    buildUi();
    onAlgorithmChanged();
    updateStepControls();
    onBuild(); // сразу показываем пример
}

QSpinBox *MainWindow::makeSpin(int minV, int maxV, int value)
{
    auto *s = new QSpinBox;
    s->setRange(minV, maxV);
    s->setValue(value);
    return s;
}

void MainWindow::buildUi()
{
    // ---------------- Левая панель управления ----------------
    auto *panel = new QWidget;
    auto *pl = new QVBoxLayout(panel);

    m_algoCombo = new QComboBox;
    const RA::Algorithm algos[] = {RA::Algorithm::StepByStep, RA::Algorithm::DDA,
                                   RA::Algorithm::BresenhamLine, RA::Algorithm::BresenhamCircle,
                                   RA::Algorithm::CastlePitteway};
    for (RA::Algorithm a : algos)
        m_algoCombo->addItem(RA::name(a), static_cast<int>(a));
    connect(m_algoCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { onAlgorithmChanged(); });

    auto *algoBox = new QGroupBox("Алгоритм");
    auto *algoLay = new QVBoxLayout(algoBox);
    algoLay->addWidget(m_algoCombo);
    pl->addWidget(algoBox);

    // Отрезок
    m_lineGroup = new QGroupBox("Отрезок");
    auto *lf = new QFormLayout(m_lineGroup);
    m_x1 = makeSpin(-1000, 1000, -5);
    m_y1 = makeSpin(-1000, 1000, -3);
    m_x2 = makeSpin(-1000, 1000, 12);
    m_y2 = makeSpin(-1000, 1000, 6);
    lf->addRow("x1:", m_x1);
    lf->addRow("y1:", m_y1);
    lf->addRow("x2:", m_x2);
    lf->addRow("y2:", m_y2);
    pl->addWidget(m_lineGroup);

    // Окружность
    m_circleGroup = new QGroupBox("Окружность");
    auto *cf = new QFormLayout(m_circleGroup);
    m_cx = makeSpin(-1000, 1000, 0);
    m_cy = makeSpin(-1000, 1000, 0);
    m_radius = makeSpin(0, 1000, 8);
    cf->addRow("Центр x:", m_cx);
    cf->addRow("Центр y:", m_cy);
    cf->addRow("Радиус R:", m_radius);
    pl->addWidget(m_circleGroup);

    m_showIdeal = new QCheckBox("Показывать идеальную фигуру (синяя)");
    m_showIdeal->setChecked(true);
    connect(m_showIdeal, &QCheckBox::toggled, this, [this](bool on) { m_canvas->setShowIdeal(on); });
    m_autoFit = new QCheckBox("Подгонять масштаб под фигуру");
    m_autoFit->setChecked(true);
    pl->addWidget(m_showIdeal);
    pl->addWidget(m_autoFit);

    // Медленная (пошаговая) растеризация
    auto *stepBox = new QGroupBox("Медленная растеризация");
    auto *sl = new QVBoxLayout(stepBox);
    m_stepMode = new QCheckBox("Рисовать по шагам (кнопка «Далее»)");
    m_stepLabel = new QLabel("Шаг: -");
    m_btnNext = new QPushButton("Далее");
    m_btnFinish = new QPushButton("Завершить");
    connect(m_btnNext, &QPushButton::clicked, this, &MainWindow::onNext);
    connect(m_btnFinish, &QPushButton::clicked, this, &MainWindow::onFinish);
    auto *stepBtns = new QHBoxLayout;
    stepBtns->addWidget(m_btnNext);
    stepBtns->addWidget(m_btnFinish);
    sl->addWidget(m_stepMode);
    sl->addWidget(m_stepLabel);
    sl->addLayout(stepBtns);
    pl->addWidget(stepBox);

    auto *btnBuild = new QPushButton("Построить");
    auto *btnCompare = new QPushButton("Сравнить время всех алгоритмов");
    auto *btnReset = new QPushButton("Сбросить вид");
    auto *btnClear = new QPushButton("Очистить");
    auto *btnHelp = new QPushButton("Справка: привязка к сетке");
    connect(btnBuild, &QPushButton::clicked, this, &MainWindow::onBuild);
    connect(btnCompare, &QPushButton::clicked, this, &MainWindow::onCompare);
    connect(btnReset, &QPushButton::clicked, this, [this] { m_canvas->resetView(); });
    connect(btnClear, &QPushButton::clicked, this, &MainWindow::onClear);
    connect(btnHelp, &QPushButton::clicked, this, &MainWindow::onHelp);
    pl->addWidget(btnBuild);
    pl->addWidget(btnCompare);
    pl->addWidget(btnReset);
    pl->addWidget(btnClear);
    pl->addWidget(btnHelp);

    // Результаты измерений
    auto *resBox = new QGroupBox("Результаты");
    auto *rl = new QVBoxLayout(resBox);
    m_countLabel = new QLabel("Пикселей: -");
    m_singleTimeLabel = new QLabel("Один запуск (с логом): -");
    m_avgTimeLabel = new QLabel("Среднее (без лога): -");
    for (QLabel *l : {m_countLabel, m_singleTimeLabel, m_avgTimeLabel}) {
        l->setWordWrap(true);
        rl->addWidget(l);
    }
    pl->addWidget(resBox);

    auto *note = new QLabel("Центр каждого пикселя (i, j) совпадает с целочисленным узлом сетки (i, j). "
                            "Ось Y направлена вверх. Колесо - зум, ЛКМ - сдвиг.");
    note->setWordWrap(true);
    note->setStyleSheet("color: #555;");
    pl->addWidget(note);
    pl->addStretch(1);
    panel->setFixedWidth(330);

    // Панель в прокручиваемой области, чтобы все элементы помещались на небольших экранах.
    auto *scroll = new QScrollArea;
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setFixedWidth(355);

    // ---------------- Холст и лог ----------------
    m_canvas = new CanvasWidget;
    connect(m_canvas, &CanvasWidget::cursorCellChanged, this, &MainWindow::onCursorCell);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setPlaceholderText("Здесь будут промежуточные значения шагов алгоритма...");

    auto *vsplit = new QSplitter(Qt::Vertical);
    vsplit->addWidget(m_canvas);
    vsplit->addWidget(m_log);
    vsplit->setStretchFactor(0, 3);
    vsplit->setStretchFactor(1, 1);

    auto *hsplit = new QSplitter(Qt::Horizontal);
    hsplit->addWidget(scroll);
    hsplit->addWidget(vsplit);
    hsplit->setStretchFactor(1, 1);
    setCentralWidget(hsplit);

    m_cursorLabel = new QLabel("Пиксель под курсором: -");
    statusBar()->addPermanentWidget(m_cursorLabel);
}

RA::Algorithm MainWindow::currentAlgorithm() const
{
    return static_cast<RA::Algorithm>(m_algoCombo->currentData().toInt());
}

RA::Params MainWindow::currentParams() const
{
    RA::Params p;
    if (RA::isCircle(currentAlgorithm())) {
        p.x1 = m_cx->value();
        p.y1 = m_cy->value();
        p.radius = m_radius->value();
    } else {
        p.x1 = m_x1->value();
        p.y1 = m_y1->value();
        p.x2 = m_x2->value();
        p.y2 = m_y2->value();
    }
    return p;
}

int MainWindow::repeatsFor(int pixelCount)
{
    // Чем больше фигура, тем меньше повторов, чтобы измерение занимало доли секунды.
    return std::clamp(500000 / std::max(1, pixelCount), 20, 5000);
}

void MainWindow::showLog(const QStringList &lines)
{
    if (lines.size() <= kMaxLogLines) {
        m_log->setPlainText(lines.join('\n'));
    } else {
        QStringList cut = lines.mid(0, kMaxLogLines);
        cut << QString("... лог обрезан: показано %1 из %2 строк").arg(kMaxLogLines).arg(lines.size());
        m_log->setPlainText(cut.join('\n'));
    }
}

void MainWindow::onAlgorithmChanged()
{
    const bool circle = RA::isCircle(currentAlgorithm());
    m_lineGroup->setVisible(!circle);
    m_circleGroup->setVisible(circle);
}

void MainWindow::onBuild()
{
    const RA::Algorithm alg = currentAlgorithm();
    const RA::Params p = currentParams();

    // 1) Один запуск с формированием лога и границ шагов - для рисования и пояснений.
    m_result = RA::run(alg, p, true);

    // 2) Точное измерение: много повторов без лога, берём среднее.
    const int repeats = repeatsFor(m_result.pixels.size());
    const double avg = RA::averageTimeUs(alg, p, repeats);

    if (RA::isCircle(alg))
        m_canvas->setIdealCircle(QPoint(p.x1, p.y1), p.radius);
    else
        m_canvas->setIdealLine(QPoint(p.x1, p.y1), QPoint(p.x2, p.y2));

    // Подгонку масштаба делаем по полной фигуре (даже если рисовать будем по шагам).
    m_canvas->setPixels(m_result.pixels);
    if (m_autoFit->isChecked())
        m_canvas->fitToShape();

    m_countLabel->setText(QString("Пикселей: %1").arg(m_result.pixels.size()));
    m_singleTimeLabel->setText(QString("Один запуск (с логом): %1 мкс").arg(m_result.timeUs, 0, 'f', 3));
    m_avgTimeLabel->setText(QString("Среднее без лога (%1 повторов):\n%2 мкс (%3 мс)")
                                .arg(repeats).arg(avg, 0, 'f', 3).arg(avg / 1000.0, 0, 'f', 5));
    m_timeLine = QString("Время (std::chrono::high_resolution_clock): один запуск с логом = %1 мкс; "
                         "среднее без лога за %2 повторов = %3 мкс")
                     .arg(m_result.timeUs, 0, 'f', 3).arg(repeats).arg(avg, 0, 'f', 3);

    m_stepIndex = -1;
    m_stepping = m_stepMode->isChecked() && !m_result.steps.isEmpty();

    if (m_stepping) {
        // Пошаговый режим: холст пуст, в логе только "заголовок" (параметры, k, b, Евклид и т.п.).
        m_canvas->setPixels(QVector<QPoint>(), m_result.pixels.size());
        m_countLabel->setText(QString("Пикселей: 0 из %1").arg(m_result.pixels.size()));
        QStringList header = m_result.log.mid(0, m_result.steps.first().logBegin);
        header << QString() << "Нажимайте «Далее» - каждый шаг рисует следующий пиксель.";
        showLog(header);
    } else {
        QStringList lines = m_result.log;
        lines << QString() << m_timeLine;
        showLog(lines);
    }
    updateStepControls();
}

void MainWindow::applyStep(int k)
{
    const RA::Step &st = m_result.steps[k];
    const int prevEnd = (k > 0) ? m_result.steps[k - 1].pixelEnd : 0;

    // Пиксели последнего шага подсвечиваются жёлтым.
    m_canvas->setPixels(m_result.pixels.mid(0, st.pixelEnd), m_result.pixels.size(), prevEnd);

    m_log->appendPlainText(QString("--- Шаг %1 из %2 ---").arg(k + 1).arg(m_result.steps.size()));
    for (int i = st.logBegin; i < st.logEnd; ++i)
        m_log->appendPlainText(m_result.log[i]);

    m_countLabel->setText(QString("Пикселей: %1 из %2").arg(st.pixelEnd).arg(m_result.pixels.size()));
    m_stepIndex = k;
}

void MainWindow::finishStepping()
{
    m_stepping = false;
    m_canvas->setPixels(m_result.pixels); // убираем подсветку, показываем итоговую раскраску
    m_countLabel->setText(QString("Пикселей: %1").arg(m_result.pixels.size()));
    m_log->appendPlainText(QString());
    m_log->appendPlainText(m_timeLine);
    updateStepControls();
}

void MainWindow::onNext()
{
    if (!m_stepping)
        return;
    const int k = m_stepIndex + 1;
    if (k >= m_result.steps.size())
        return;
    applyStep(k);
    if (k == m_result.steps.size() - 1)
        finishStepping();
    else
        updateStepControls();
}

void MainWindow::onFinish()
{
    if (!m_stepping)
        return;
    // Дописываем в лог все оставшиеся шаги одним куском (быстрее, чем по одному).
    QStringList lines;
    for (int k = m_stepIndex + 1; k < m_result.steps.size(); ++k) {
        const RA::Step &st = m_result.steps[k];
        lines << QString("--- Шаг %1 из %2 ---").arg(k + 1).arg(m_result.steps.size());
        for (int i = st.logBegin; i < st.logEnd; ++i)
            lines << m_result.log[i];
    }
    m_log->appendPlainText(lines.join('\n'));
    m_stepIndex = m_result.steps.size() - 1;
    finishStepping();
}

void MainWindow::updateStepControls()
{
    m_btnNext->setEnabled(m_stepping);
    m_btnFinish->setEnabled(m_stepping);
    if (m_stepping)
        m_stepLabel->setText(QString("Шаг: %1 / %2").arg(m_stepIndex + 1).arg(m_result.steps.size()));
    else if (m_stepIndex >= 0)
        m_stepLabel->setText(QString("Шаг: %1 / %1 (готово)").arg(m_result.steps.size()));
    else
        m_stepLabel->setText("Шаг: -");
}

void MainWindow::onCompare()
{
    m_stepping = false; // лог будет заменён таблицей - пошаговый режим прерываем
    updateStepControls();
    // Сравнение четырёх алгоритмов для отрезка и отдельно - окружности.
    RA::Params lineP;
    lineP.x1 = m_x1->value();
    lineP.y1 = m_y1->value();
    lineP.x2 = m_x2->value();
    lineP.y2 = m_y2->value();

    RA::Params circP;
    circP.x1 = m_cx->value();
    circP.y1 = m_cy->value();
    circP.radius = m_radius->value();

    QStringList out;
    out << QString("=== Сравнение времени выполнения ===")
        << QString("Отрезок (%1; %2) -> (%3; %4)").arg(lineP.x1).arg(lineP.y1).arg(lineP.x2).arg(lineP.y2);

    const RA::Algorithm lineAlgos[] = {RA::Algorithm::StepByStep, RA::Algorithm::DDA,
                                       RA::Algorithm::BresenhamLine, RA::Algorithm::CastlePitteway};
    for (RA::Algorithm a : lineAlgos) {
        const int px = RA::run(a, lineP, false).pixels.size();
        const int rep = repeatsFor(px);
        const double t = RA::averageTimeUs(a, lineP, rep);
        out << QString("%1 | пикселей: %2 | %3 мкс (среднее по %4 повторов)")
                   .arg(RA::name(a), -36).arg(px, 5).arg(t, 10, 'f', 3).arg(rep);
    }

    out << QString() << QString("Окружность: центр (%1; %2), R=%3").arg(circP.x1).arg(circP.y1).arg(circP.radius);
    {
        const RA::Algorithm a = RA::Algorithm::BresenhamCircle;
        const int px = RA::run(a, circP, false).pixels.size();
        const int rep = repeatsFor(px);
        const double t = RA::averageTimeUs(a, circP, rep);
        out << QString("%1 | пикселей: %2 | %3 мкс (среднее по %4 повторов)")
                   .arg(RA::name(a), -36).arg(px, 5).arg(t, 10, 'f', 3).arg(rep);
    }
    out << QString() << "Замечание: время зависит от компилятора, оптимизаций и загрузки процессора; "
                        "для отчёта собирайте проект в Release.";
    showLog(out);
}

void MainWindow::onClear()
{
    m_stepping = false;
    m_stepIndex = -1;
    m_result = RA::Result();
    updateStepControls();
    m_canvas->clearAll();
    m_log->clear();
    m_countLabel->setText("Пикселей: -");
    m_singleTimeLabel->setText("Один запуск (с логом): -");
    m_avgTimeLabel->setText("Среднее (без лога): -");
}

void MainWindow::onCursorCell(const QPoint &cell)
{
    m_cursorLabel->setText(QString("Пиксель под курсором: (%1; %2)").arg(cell.x()).arg(cell.y()));
}

void MainWindow::onHelp()
{
    QMessageBox::information(
        this, "Привязка к пиксельной сетке",
        "Дискретные координаты и сетка\n\n"
        "- Все алгоритмы работают с целочисленными координатами (i, j).\n"
        "- Пиксель (i, j) - единичный квадрат, центр которого лежит в узле сетки (i, j), "
        "т.е. в точке пересечения линий сетки с координатами x = i и y = j.\n"
        "- Линии сетки и подписи делений проходят через центры пикселей; закрашенный квадрат "
        "пикселя занимает область [i-0.5; i+0.5] x [j-0.5; j+0.5].\n"
        "- Ось X направлена вправо, ось Y - вверх (экранная ось Y инвертируется при отрисовке: "
        "sy = origin.y - Y*scale).\n"
        "- Вещественные значения (y = k*x + b, x/y в ЦДА) округляются к ближайшему целому: "
        "floor(v + 0.5).\n\n"
        "Цвета: зелёный - начало отрезка, оранжевый - конец, красный - остальные пиксели, "
        "синяя линия - идеальная математическая фигура.\n\n"
        "Управление: колесо мыши - масштаб относительно курсора, левая кнопка и перетаскивание - "
        "сдвиг.");
}
