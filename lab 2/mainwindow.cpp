// ============================================================================
//  mainwindow.cpp — интерфейс собран в коде (без .ui)
// ============================================================================
#include "mainwindow.h"

#include "imageview.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

using huff::HuffmanCompressor;

namespace {

/// Курсор ожидания на время тяжёлой операции (RAII).
struct WaitCursor {
    WaitCursor()
    {
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        QGuiApplication::processEvents();
    }
    ~WaitCursor() { QGuiApplication::restoreOverrideCursor(); }
};

QGroupBox* titled(const QString& title, QWidget* inner)
{
    auto* box = new QGroupBox(title);
    auto* lay = new QVBoxLayout(box);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->addWidget(inner);
    return box;
}

QString formatBytes(std::uint64_t bytes)
{
    return QStringLiteral("%1 Б (%2 КБ)")
        .arg(static_cast<qulonglong>(bytes))
        .arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 2);
}

QString formatMs(double ms)
{
    return QStringLiteral("%1 мс").arg(ms, 0, 'f', 2);
}

} // namespace

// ============================================================================
//  Конструктор и построение интерфейса
// ============================================================================
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Лабораторная работа №2 — Вариант 10: пороги, фильтры, сжатие Хаффмана"));

    auto* tabs = new QTabWidget(this);
    tabs->addTab(buildFilterTab(),  tr("Фильтрация и Пороговая обработка (Вариант 10)"));
    tabs->addTab(buildHuffmanTab(), tr("Сжатие Хаффмана"));
    setCentralWidget(tabs);

    buildMenus();
    statusBar()->showMessage(tr("Откройте изображение: Файл → Открыть (Ctrl+O)"));
    resetHuffStats();
}

void MainWindow::buildMenus()
{
    auto* fileMenu = menuBar()->addMenu(tr("&Файл"));

    auto* openAct = fileMenu->addAction(tr("&Открыть изображение…"));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::openImage);

    auto* saveAct = fileMenu->addAction(tr("&Сохранить результат обработки…"));
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, &MainWindow::saveProcessed);

    fileMenu->addSeparator();
    auto* quitAct = fileMenu->addAction(tr("&Выход"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu(tr("&Вид"));
    auto* fitAct = viewMenu->addAction(tr("Подогнать под размер окна"));
    fitAct->setCheckable(true);
    fitAct->setChecked(true);
    connect(fitAct, &QAction::toggled, this, [this](bool fit) {
        for (ImageView* v : std::as_const(m_views))
            v->setFit(fit);
    });
}

QWidget* MainWindow::buildFilterTab()
{
    auto* page = new QWidget;
    auto* root = new QHBoxLayout(page);

    // ---- Side-by-Side просмотр ------------------------------------------------------
    m_viewOrig1 = new ImageView;
    m_viewProc  = new ImageView;
    m_views << m_viewOrig1 << m_viewProc;

    auto* views = new QHBoxLayout;
    views->addWidget(titled(tr("Исходное"), m_viewOrig1), 1);
    views->addWidget(titled(tr("Обработанное"), m_viewProc), 1);
    root->addLayout(views, 1);

    // Синхронная прокрутка в режиме 100 %.
    auto sync = [](QScrollBar* a, QScrollBar* b) {
        QObject::connect(a, &QScrollBar::valueChanged, b, &QScrollBar::setValue);
        QObject::connect(b, &QScrollBar::valueChanged, a, &QScrollBar::setValue);
    };
    sync(m_viewOrig1->horizontalScrollBar(), m_viewProc->horizontalScrollBar());
    sync(m_viewOrig1->verticalScrollBar(),   m_viewProc->verticalScrollBar());

    // ---- Панель параметров --------------------------------------------------------------
    auto* panel = new QWidget;
    panel->setFixedWidth(330);
    auto* pl = new QVBoxLayout(panel);

    // Глобальная пороговая обработка
    auto* thrBox = new QGroupBox(tr("Глобальная пороговая обработка"));
    auto* tl = new QVBoxLayout(thrBox);
    m_rbIter   = new QRadioButton(tr("Итеративный (среднее фона и объекта)"));
    m_rbOtsu   = new QRadioButton(tr("Метод Оцу (макс. межклассовой дисперсии)"));
    m_rbManual = new QRadioButton(tr("Ручной порог T"));
    m_rbOtsu->setChecked(true);
    tl->addWidget(m_rbIter);
    tl->addWidget(m_rbOtsu);
    tl->addWidget(m_rbManual);

    m_thrSlider = new QSlider(Qt::Horizontal);
    m_thrSlider->setRange(0, 255);
    m_thrSlider->setValue(128);
    m_thrSpin = new QSpinBox;
    m_thrSpin->setRange(0, 255);
    m_thrSpin->setValue(128);
    auto* sliderRow = new QHBoxLayout;
    sliderRow->addWidget(m_thrSlider, 1);
    sliderRow->addWidget(m_thrSpin);
    tl->addLayout(sliderRow);
    m_thrSlider->setEnabled(false);
    m_thrSpin->setEnabled(false);

    m_lblThreshold = new QLabel(tr("Порог T = —"));
    tl->addWidget(m_lblThreshold);

    m_histLabel = new QLabel;
    m_histLabel->setFixedSize(256, 100);
    m_histLabel->setFrameShape(QFrame::StyledPanel);
    tl->addWidget(m_histLabel, 0, Qt::AlignHCenter);

    m_chkChain = new QCheckBox(tr("Порог — к результату фильтра (цепочка)"));
    tl->addWidget(m_chkChain);

    auto* btnThr = new QPushButton(tr("Применить порог"));
    tl->addWidget(btnThr);
    pl->addWidget(thrBox);

    // Фильтры порядковых статистик
    auto* fltBox = new QGroupBox(tr("Фильтр порядковых статистик"));
    auto* form = new QFormLayout(fltBox);

    m_cmbFilter = new QComboBox;
    m_cmbFilter->addItem(tr("Медиана"), static_cast<int>(ImageProcessor::OrderFilter::Median));
    m_cmbFilter->addItem(tr("Минимум (эрозия)"), static_cast<int>(ImageProcessor::OrderFilter::Min));
    m_cmbFilter->addItem(tr("Максимум (дилатация)"), static_cast<int>(ImageProcessor::OrderFilter::Max));
    m_cmbFilter->addItem(tr("Средняя точка"), static_cast<int>(ImageProcessor::OrderFilter::Midpoint));

    m_cmbKernel = new QComboBox;
    m_cmbKernel->addItem(QStringLiteral("3×3"), 3);
    m_cmbKernel->addItem(QStringLiteral("5×5"), 5);
    m_cmbKernel->addItem(QStringLiteral("7×7"), 7);

    m_cmbShape = new QComboBox;
    m_cmbShape->addItem(tr("Квадрат"), static_cast<int>(ImageProcessor::KernelShape::Square));
    m_cmbShape->addItem(tr("Крест"), static_cast<int>(ImageProcessor::KernelShape::Cross));

    m_cmbBorder = new QComboBox;
    m_cmbBorder->addItem(tr("Повтор края (Clamp to Edge)"), static_cast<int>(ImageProcessor::BorderMode::Replicate));
    m_cmbBorder->addItem(tr("Отражение (Reflect)"), static_cast<int>(ImageProcessor::BorderMode::Reflect));
    m_cmbBorder->addItem(tr("Нули (Zero Padding)"), static_cast<int>(ImageProcessor::BorderMode::Zero));

    form->addRow(tr("Фильтр:"), m_cmbFilter);
    form->addRow(tr("Размер окна:"), m_cmbKernel);
    form->addRow(tr("Форма окна:"), m_cmbShape);
    form->addRow(tr("Границы:"), m_cmbBorder);

    auto* btnFlt = new QPushButton(tr("Применить фильтр"));
    form->addRow(btnFlt);
    pl->addWidget(fltBox);

    auto* btnReset = new QPushButton(tr("Сбросить результат"));
    auto* btnSave  = new QPushButton(tr("Сохранить результат…"));
    pl->addWidget(btnReset);
    pl->addWidget(btnSave);
    pl->addStretch(1);
    root->addWidget(panel);

    // ---- соединения ----------------------------------------------------------------------
    for (QRadioButton* rb : {m_rbIter, m_rbOtsu, m_rbManual}) {
        connect(rb, &QRadioButton::toggled, this, [this](bool on) {
            if (on) onThresholdMethodChanged();
        });
    }
    connect(m_thrSlider, &QSlider::valueChanged, m_thrSpin, &QSpinBox::setValue);
    connect(m_thrSpin, &QSpinBox::valueChanged, m_thrSlider, &QSlider::setValue);
    connect(m_thrSlider, &QSlider::valueChanged, this, &MainWindow::onThresholdSliderChanged);
    connect(btnThr,   &QPushButton::clicked, this, &MainWindow::applyThreshold);
    connect(btnFlt,   &QPushButton::clicked, this, &MainWindow::applyFilter);
    connect(btnReset, &QPushButton::clicked, this, &MainWindow::resetProcessing);
    connect(btnSave,  &QPushButton::clicked, this, &MainWindow::saveProcessed);

    return page;
}

QWidget* MainWindow::buildHuffmanTab()
{
    auto* page = new QWidget;
    auto* root = new QHBoxLayout(page);

    // ---- просмотр ------------------------------------------------------------------------
    m_viewOrig2 = new ImageView;
    m_viewDec   = new ImageView;
    m_views << m_viewOrig2 << m_viewDec;

    auto* views = new QHBoxLayout;
    views->addWidget(titled(tr("Исходное"), m_viewOrig2), 1);
    views->addWidget(titled(tr("Восстановленное из .huff"), m_viewDec), 1);
    root->addLayout(views, 1);

    // ---- панель управления и статистики -----------------------------------------------------
    auto* panel = new QWidget;
    panel->setFixedWidth(380);
    auto* pl = new QVBoxLayout(panel);

    auto* btnEnc = new QPushButton(tr("Сжать в .huff…"));
    auto* btnDec = new QPushButton(tr("Распаковать .huff…"));
    pl->addWidget(btnEnc);
    pl->addWidget(btnDec);

    auto* statBox = new QGroupBox(tr("Метрики и статистика"));
    auto* form = new QFormLayout(statBox);
    auto makeLabel = [&](const QString& caption) {
        auto* l = new QLabel(QStringLiteral("—"));
        l->setWordWrap(true);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(caption, l);
        return l;
    };
    m_lblSrcFile = makeLabel(tr("Файл-источник на диске:"));
    m_lblRaw     = makeLabel(tr("Исходные данные (RAW):"));
    m_lblComp    = makeLabel(tr("Сжатый файл .huff:"));
    m_lblRatio   = makeLabel(tr("Коэффициент сжатия:"));
    m_lblSaving  = makeLabel(tr("Экономия места:"));
    m_lblEntropy = makeLabel(tr("Энтропия H:"));
    m_lblAvg     = makeLabel(tr("Средняя длина кода L_avg:"));
    m_lblTEnc    = makeLabel(tr("Время сжатия:"));
    m_lblTDec    = makeLabel(tr("Время декодирования:"));
    m_lblVerify  = makeLabel(tr("Lossless-проверка:"));
    pl->addWidget(statBox);
    pl->addStretch(1);
    root->addWidget(panel);

    connect(btnEnc, &QPushButton::clicked, this, &MainWindow::compressToHuff);
    connect(btnDec, &QPushButton::clicked, this, &MainWindow::decompressFromHuff);
    return page;
}

// ============================================================================
//  Общие слоты
// ============================================================================
void MainWindow::openImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Открыть изображение"), QString(),
        tr("Изображения (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.gif *.ppm *.pgm *.pbm);;Все файлы (*)"));
    if (path.isEmpty())
        return;

    const QImage img(path);
    if (img.isNull()) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось прочитать изображение:\n%1").arg(path));
        return;
    }
    setOriginal(HuffmanCompressor::normalizeFormat(img), path);
}

void MainWindow::setOriginal(const QImage& image, const QString& path)
{
    m_original = image;
    m_originalPath = path;
    m_filtered = QImage();
    m_processed = QImage();

    m_viewOrig1->setImage(m_original);
    m_viewOrig2->setImage(m_original);
    m_viewProc->clear();
    m_viewDec->clear();
    m_histLabel->clear();
    m_lblThreshold->setText(tr("Порог T = —"));
    resetHuffStats();
    m_lblSrcFile->setText(formatBytes(static_cast<std::uint64_t>(QFileInfo(path).size())));

    setWindowTitle(tr("Лаб. №2 — %1 (%2×%3)")
                       .arg(QFileInfo(path).fileName())
                       .arg(image.width())
                       .arg(image.height()));
    statusBar()->showMessage(tr("Загружено: %1, %2×%3").arg(path).arg(image.width()).arg(image.height()));
}

void MainWindow::saveProcessed()
{
    if (m_processed.isNull()) {
        QMessageBox::information(this, tr("Сохранение"), tr("Пока нечего сохранять: сначала примените порог или фильтр."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Сохранить результат"), QString(),
        tr("PNG (*.png);;BMP (*.bmp);;JPEG (*.jpg)"));
    if (path.isEmpty())
        return;
    if (!m_processed.save(path))
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить файл."));
    else
        statusBar()->showMessage(tr("Результат сохранён: %1").arg(path));
}

// ============================================================================
//  Вкладка 1: пороговая обработка и фильтры
// ============================================================================
void MainWindow::onThresholdMethodChanged()
{
    const bool manual = m_rbManual->isChecked();
    m_thrSlider->setEnabled(manual);
    m_thrSpin->setEnabled(manual);
    if (!m_original.isNull())
        applyThreshold();
}

void MainWindow::onThresholdSliderChanged(int)
{
    // В ручном режиме результат обновляется «вживую» — пороговая обработка дешёвая.
    if (m_rbManual->isChecked() && !m_original.isNull())
        applyThreshold();
}

void MainWindow::applyThreshold()
{
    const QImage& base = (m_chkChain->isChecked() && !m_filtered.isNull()) ? m_filtered : m_original;
    if (base.isNull()) {
        statusBar()->showMessage(tr("Сначала откройте изображение."));
        return;
    }

    QElapsedTimer timer;
    timer.start();

    const QImage gray = ImageProcessor::toGrayscale(base);
    const auto hist = ImageProcessor::computeHistogram(gray);

    int t = 0;
    QString method;
    if (m_rbIter->isChecked()) {
        t = ImageProcessor::iterativeThreshold(hist);
        method = tr("итеративный");
    } else if (m_rbOtsu->isChecked()) {
        t = ImageProcessor::otsuThreshold(hist);
        method = tr("Оцу");
    } else {
        t = m_thrSlider->value();
        method = tr("ручной");
    }

    if (!m_rbManual->isChecked()) {
        // Показываем вычисленный порог в слайдере, не запуская повторную обработку.
        const QSignalBlocker b1(m_thrSlider);
        const QSignalBlocker b2(m_thrSpin);
        m_thrSlider->setValue(t);
        m_thrSpin->setValue(t);
    }

    m_processed = ImageProcessor::applyThreshold(gray, t);
    m_viewProc->setImage(m_processed);
    showHistogram(hist, t);
    m_lblThreshold->setText(tr("Порог T = %1").arg(t));

    statusBar()->showMessage(tr("Порог (%1): T = %2 — %3").arg(method).arg(t).arg(formatMs(timer.nsecsElapsed() / 1e6)));
}

void MainWindow::applyFilter()
{
    if (m_original.isNull()) {
        statusBar()->showMessage(tr("Сначала откройте изображение."));
        return;
    }
    const auto filter = static_cast<ImageProcessor::OrderFilter>(m_cmbFilter->currentData().toInt());
    const int k = m_cmbKernel->currentData().toInt();
    const auto shape = static_cast<ImageProcessor::KernelShape>(m_cmbShape->currentData().toInt());
    const auto border = static_cast<ImageProcessor::BorderMode>(m_cmbBorder->currentData().toInt());

    WaitCursor wait;
    QElapsedTimer timer;
    timer.start();

    m_filtered = ImageProcessor::orderStatisticFilter(m_original, filter, k, shape, border);
    m_processed = m_filtered;
    m_viewProc->setImage(m_processed);

    statusBar()->showMessage(tr("Фильтр «%1» %2×%2, %3, границы: %4 — %5")
                                 .arg(m_cmbFilter->currentText())
                                 .arg(k)
                                 .arg(m_cmbShape->currentText().toLower())
                                 .arg(m_cmbBorder->currentText())
                                 .arg(formatMs(timer.nsecsElapsed() / 1e6)));
}

void MainWindow::resetProcessing()
{
    m_filtered = QImage();
    m_processed = QImage();
    m_viewProc->clear();
    m_histLabel->clear();
    m_lblThreshold->setText(tr("Порог T = —"));
    statusBar()->showMessage(tr("Результат сброшен."));
}

void MainWindow::showHistogram(const ImageProcessor::Histogram& hist, int threshold)
{
    constexpr int W = 256, H = 100;
    QPixmap pm(W, H);
    pm.fill(palette().base().color());

    std::uint32_t maxCount = 1;
    for (std::uint32_t c : hist)
        maxCount = std::max(maxCount, c);

    QPainter p(&pm);
    p.setPen(palette().mid().color());
    const double denom = std::log1p(static_cast<double>(maxCount));   // логарифмическая шкала
    for (int i = 0; i < W; ++i) {
        const int hgt = static_cast<int>(std::log1p(static_cast<double>(hist[static_cast<std::size_t>(i)])) / denom * (H - 2));
        p.drawLine(i, H - 1, i, H - 1 - hgt);
    }
    p.setPen(QPen(Qt::red, 1));
    p.drawLine(threshold, 0, threshold, H - 1);          // положение порога T
    p.end();
    m_histLabel->setPixmap(pm);
}

// ============================================================================
//  Вкладка 2: сжатие Хаффмана
// ============================================================================
void MainWindow::resetHuffStats()
{
    for (QLabel* l : {m_lblRaw, m_lblComp, m_lblRatio, m_lblSaving, m_lblEntropy,
                      m_lblAvg, m_lblTEnc, m_lblTDec, m_lblVerify})
        l->setText(QStringLiteral("—"));
    m_lblSrcFile->setText(QStringLiteral("—"));
    m_encodeMs = m_decodeMs = -1.0;
}

void MainWindow::showHuffStats(const huff::Stats& s)
{
    m_lblRaw->setText(formatBytes(s.originalBytes));
    m_lblComp->setText(tr("%1\n(заголовок %2 Б + поток %3 Б)")
                           .arg(formatBytes(s.compressedBytes))
                           .arg(static_cast<qulonglong>(s.headerBytes))
                           .arg(static_cast<qulonglong>(s.payloadBytes)));
    m_lblRatio->setText(QString::number(s.ratio, 'f', 4));
    m_lblSaving->setText(QStringLiteral("%1 %").arg(s.savingPercent, 0, 'f', 2));
    m_lblEntropy->setText(tr("%1 бит/символ").arg(s.entropy, 0, 'f', 4));
    m_lblAvg->setText(tr("%1 бит/символ").arg(s.avgCodeLength, 0, 'f', 4));
    m_lblTEnc->setText(m_encodeMs >= 0 ? formatMs(m_encodeMs) : QStringLiteral("—"));
    m_lblTDec->setText(m_decodeMs >= 0 ? formatMs(m_decodeMs) : QStringLiteral("—"));
}

void MainWindow::compressToHuff()
{
    if (m_original.isNull()) {
        QMessageBox::information(this, tr("Сжатие"), tr("Сначала откройте изображение."));
        return;
    }

    const QFileInfo srcInfo(m_originalPath);
    const QString suggested = srcInfo.dir().filePath(srcInfo.completeBaseName() + QStringLiteral(".huff"));
    QString path = QFileDialog::getSaveFileName(this, tr("Сохранить как .huff"), suggested, tr("Huffman (*.huff)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".huff"), Qt::CaseInsensitive))
        path += QStringLiteral(".huff");

    try {
        WaitCursor wait;
        QElapsedTimer timer;

        // 1. Сжатие (замеряется только алгоритм, без записи на диск).
        timer.start();
        const huff::CompressResult enc = HuffmanCompressor::compress(m_original);
        m_encodeMs = timer.nsecsElapsed() / 1e6;

        QFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(enc.data) != enc.data.size())
            throw std::runtime_error(tr("Не удалось записать файл").toStdString());
        out.close();

        // 2. Немедленное декодирование из памяти и проверка на отсутствие потерь.
        timer.restart();
        const huff::DecompressResult dec = HuffmanCompressor::decompress(enc.data);
        m_decodeMs = timer.nsecsElapsed() / 1e6;

        const bool identical = dec.checksumOk && HuffmanCompressor::imagesEqual(m_original, dec.image);

        m_viewDec->setImage(dec.image);
        showHuffStats(enc.stats);
        m_lblVerify->setText(identical ? tr("✔ изображения идентичны (побайтово), FNV-1a совпала")
                                       : tr("✘ ОШИБКА: восстановленное изображение отличается!"));
        statusBar()->showMessage(tr("Сжато: %1 → %2 Б, K = %3, сжатие %4, декодирование %5")
                                     .arg(static_cast<qulonglong>(enc.stats.originalBytes))
                                     .arg(static_cast<qulonglong>(enc.stats.compressedBytes))
                                     .arg(enc.stats.ratio, 0, 'f', 3)
                                     .arg(formatMs(m_encodeMs))
                                     .arg(formatMs(m_decodeMs)));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Ошибка сжатия"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::decompressFromHuff()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Открыть .huff"), QString(), tr("Huffman (*.huff);;Все файлы (*)"));
    if (path.isEmpty())
        return;

    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось открыть файл."));
        return;
    }
    const QByteArray data = in.readAll();
    in.close();

    try {
        WaitCursor wait;
        QElapsedTimer timer;
        timer.start();
        const huff::DecompressResult dec = HuffmanCompressor::decompress(data);
        m_decodeMs = timer.nsecsElapsed() / 1e6;

        m_viewDec->setImage(dec.image);
        m_encodeMs = -1.0;                               // время сжатия к этому файлу неизвестно
        showHuffStats(dec.stats);

        QString verdict = dec.checksumOk ? tr("✔ контрольная сумма FNV-1a совпала")
                                         : tr("✘ контрольная сумма НЕ совпала (файл повреждён)");
        if (!m_original.isNull()) {
            verdict += HuffmanCompressor::imagesEqual(m_original, dec.image)
                           ? tr("\n✔ совпадает с загруженным оригиналом")
                           : tr("\n• отличается от загруженного оригинала (возможно, другое изображение)");
        }
        m_lblVerify->setText(verdict);
        statusBar()->showMessage(tr("Распаковано: %1 (%2×%3), декодирование %4")
                                     .arg(QFileInfo(path).fileName())
                                     .arg(dec.image.width())
                                     .arg(dec.image.height())
                                     .arg(formatMs(m_decodeMs)));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Ошибка распаковки"), QString::fromUtf8(e.what()));
    }
}
