// ============================================================================
//  mainwindow.h — главное окно (только GUI: отображение и вызовы бизнес-логики)
// ============================================================================
#pragma once

#include <QImage>
#include <QList>
#include <QMainWindow>
#include <QString>

#include "huffmancore.h"
#include "imageprocessor.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QRadioButton;
class QSlider;
class QSpinBox;
class QWidget;
class ImageView;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    // Общие
    void openImage();
    void saveProcessed();

    // Вкладка 1: фильтрация и пороговая обработка
    void onThresholdMethodChanged();
    void onThresholdSliderChanged(int value);
    void applyThreshold();
    void applyFilter();
    void resetProcessing();

    // Вкладка 2: сжатие Хаффмана
    void compressToHuff();
    void decompressFromHuff();

private:
    void buildMenus();
    QWidget* buildFilterTab();
    QWidget* buildHuffmanTab();

    void setOriginal(const QImage& image, const QString& path);
    void showHistogram(const ImageProcessor::Histogram& hist, int threshold);
    void showHuffStats(const huff::Stats& stats);
    void resetHuffStats();

    // --- данные --------------------------------------------------------------------
    QImage  m_original;      // загруженное (нормализованное) изображение
    QImage  m_filtered;      // результат последнего фильтра (вход для порога в режиме «цепочка»)
    QImage  m_processed;     // то, что показано справа на вкладке 1
    QString m_originalPath;
    double  m_encodeMs = -1.0;
    double  m_decodeMs = -1.0;

    QList<ImageView*> m_views;

    // --- вкладка 1 -------------------------------------------------------------------
    ImageView*    m_viewOrig1 = nullptr;
    ImageView*    m_viewProc  = nullptr;
    QRadioButton* m_rbIter    = nullptr;
    QRadioButton* m_rbOtsu    = nullptr;
    QRadioButton* m_rbManual  = nullptr;
    QSlider*      m_thrSlider = nullptr;
    QSpinBox*     m_thrSpin   = nullptr;
    QLabel*       m_lblThreshold = nullptr;
    QLabel*       m_histLabel = nullptr;
    QCheckBox*    m_chkChain  = nullptr;
    QComboBox*    m_cmbFilter = nullptr;
    QComboBox*    m_cmbKernel = nullptr;
    QComboBox*    m_cmbShape  = nullptr;
    QComboBox*    m_cmbBorder = nullptr;

    // --- вкладка 2 -------------------------------------------------------------------
    ImageView* m_viewOrig2 = nullptr;
    ImageView* m_viewDec   = nullptr;
    QLabel* m_lblSrcFile = nullptr;
    QLabel* m_lblRaw     = nullptr;
    QLabel* m_lblComp    = nullptr;
    QLabel* m_lblRatio   = nullptr;
    QLabel* m_lblSaving  = nullptr;
    QLabel* m_lblEntropy = nullptr;
    QLabel* m_lblAvg     = nullptr;
    QLabel* m_lblTEnc    = nullptr;
    QLabel* m_lblTDec    = nullptr;
    QLabel* m_lblVerify  = nullptr;
};
