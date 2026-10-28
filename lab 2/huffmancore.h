// ============================================================================
//  huffmancore.h — сжатие графической информации методом Хаффмана (без потерь)
//
//  Формат файла .huff (все многобайтные числа — little-endian):
//
//    смещение  размер  поле
//    0         4       сигнатура 'H','U','F','F'
//    4         1       версия формата (1)
//    5         1       код формата QImage: 0 = Grayscale8, 1 = RGB32, 2 = ARGB32
//    6         1       число каналов (1 / 3 / 4)
//    7         4       ширина  W
//    11        4       высота  H
//    15        8       число байт данных (W·H·каналы)
//    23        4       контрольная сумма FNV-1a (32 бит) исходных данных
//    27        2       число различных байтов N (1..256)
//    29        5·N     таблица частот: {байт: u8, частота: u32}, по возрастанию байта
//    29+5·N    ...     битовый поток кодов Хаффмана (MSB первым, добор нулями)
//
//  Дерево не хранится: декодер восстанавливает его из таблицы частот тем же
//  детерминированным алгоритмом (тай-брейк по порядковому номеру узла).
//
//  Данные кодируются как поток байтов в порядке «строка за строкой»:
//    Grayscale8 — Y;  RGB32 — R,G,B;  ARGB32 — R,G,B,A.
//  Формат не зависит от порядка байтов платформы.
// ============================================================================
#pragma once

#include <QByteArray>
#include <QImage>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace huff {

/// Таблица частот байтов 0..255.
using FreqTable = std::array<std::uint32_t, 256>;

/// Узел дерева Хаффмана. Узлы лежат в std::vector и ссылаются друг на друга
/// индексами (без сырых указателей и ручного управления памятью).
struct Node {
    int           symbol = -1;   ///< значение байта (только у листа)
    std::uint64_t freq   = 0;    ///< суммарная частота поддерева
    int           left   = -1;   ///< индекс левого потомка (бит 0)
    int           right  = -1;   ///< индекс правого потомка (бит 1)

    bool isLeaf() const noexcept { return left < 0 && right < 0; }
};

/// Префиксный код одного символа: младшие `length` бит поля `bits`.
struct Code {
    std::uint64_t bits   = 0;
    std::uint8_t  length = 0;
};

// ----------------------------------------------------------------------------
//  BitWriter — побитовая упаковка кодов в байтовый буфер
// ----------------------------------------------------------------------------
class BitWriter {
public:
    void reserve(std::size_t bytes) { m_buffer.reserve(bytes); }

    /// Записывает `length` (0..56) младших бит `code`, старший бит — первым.
    void writeBits(std::uint64_t code, unsigned length)
    {
        if (length == 0)
            return;
        // Сдвигаем накопитель влево и «подмешиваем» новые биты операцией |.
        m_acc = (m_acc << length) | (code & ((std::uint64_t{1} << length) - 1));
        m_count += length;
        // Каждый набранный байт выталкиваем в буфер.
        while (m_count >= 8) {
            m_count -= 8;
            m_buffer.push_back(static_cast<std::uint8_t>(m_acc >> m_count));
        }
    }

    /// Дописывает неполный последний байт, добивая его нулевыми битами справа.
    void flush()
    {
        if (m_count > 0) {
            m_buffer.push_back(static_cast<std::uint8_t>(m_acc << (8 - m_count)));
            m_count = 0;
            m_acc = 0;
        }
    }

    const std::vector<std::uint8_t>& bytes() const noexcept { return m_buffer; }

private:
    std::uint64_t              m_acc   = 0;   // накопитель (значимы младшие m_count бит)
    unsigned                   m_count = 0;   // число значимых бит в накопителе (< 8 между вызовами)
    std::vector<std::uint8_t>  m_buffer;
};

// ----------------------------------------------------------------------------
//  BitReader — побитовое чтение из байтового буфера
// ----------------------------------------------------------------------------
class BitReader {
public:
    BitReader(const std::uint8_t* data, std::size_t size) noexcept
        : m_data(data), m_size(size) {}

    /// Возвращает следующий бит (0/1). Бросает исключение при выходе за буфер.
    int readBit()
    {
        if (m_byte >= m_size)
            throw std::runtime_error("Неожиданный конец битового потока");
        const int bit = (m_data[m_byte] >> (7 - m_bit)) & 1;
        if (++m_bit == 8) {
            m_bit = 0;
            ++m_byte;
        }
        return bit;
    }

private:
    const std::uint8_t* m_data;
    std::size_t         m_size;
    std::size_t         m_byte = 0;
    unsigned            m_bit  = 0;
};

// ----------------------------------------------------------------------------
//  Статистика сжатия
// ----------------------------------------------------------------------------
struct Stats {
    std::uint64_t originalBytes   = 0;   ///< размер несжатых данных (W·H·каналы)
    std::uint64_t compressedBytes = 0;   ///< размер всего .huff файла
    std::uint64_t headerBytes     = 0;   ///< размер заголовка с таблицей частот
    std::uint64_t payloadBytes    = 0;   ///< размер битового потока
    double entropy       = 0.0;          ///< H = −Σ p·log2 p, бит/символ
    double avgCodeLength = 0.0;          ///< L_avg = Σ p·len, бит/символ
    double ratio         = 0.0;          ///< Compression Ratio = original / compressed
    double savingPercent = 0.0;          ///< Space Saving % = (1 − compressed/original)·100
};

struct CompressResult {
    QByteArray data;                     ///< содержимое .huff файла
    Stats      stats;
};

struct DecompressResult {
    QImage image;                        ///< восстановленное изображение
    Stats  stats;
    bool   checksumOk = false;           ///< совпала ли контрольная сумма FNV-1a
};

// ----------------------------------------------------------------------------
//  HuffmanCompressor
//  Все методы бросают std::runtime_error (сообщение в UTF-8) при ошибке.
// ----------------------------------------------------------------------------
class HuffmanCompressor {
public:
    /// Приводит изображение к одному из поддерживаемых форматов:
    /// Grayscale8 (для серых), RGB32 или ARGB32 (при наличии альфа-канала).
    static QImage normalizeFormat(const QImage& img);

    /// Сжатие изображения в содержимое .huff файла.
    static CompressResult compress(const QImage& img);

    /// Распаковка содержимого .huff файла.
    static DecompressResult decompress(const QByteArray& file);

    /// Точное попиксельное сравнение (Lossless Verification).
    static bool imagesEqual(const QImage& a, const QImage& b);

private:
    struct Tree {
        std::vector<Node> nodes;
        int root = -1;
    };

    static FreqTable countFrequencies(const std::vector<std::uint8_t>& data);
    static Tree buildTree(const FreqTable& freq);
    static std::array<Code, 256> buildCodes(const Tree& tree);
    static void fillCodeStats(const FreqTable& freq,
                              const std::array<Code, 256>& codes,
                              std::uint64_t total,
                              Stats& stats);
};

} // namespace huff
