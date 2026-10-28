// ============================================================================
//  main.cpp — точка входа
// ============================================================================
#include <QApplication>

#include "mainwindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Lab2 Huffman + Threshold"));

    MainWindow window;
    window.resize(1400, 850);
    window.show();

    return app.exec();
}
