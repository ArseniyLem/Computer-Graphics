// ============================================================================
//  huffmancore.cpp — реализация сжатия Хаффмана. Описание формата см. в .h
//  Никаких zlib/libpng: битовая упаковка и дерево написаны вручную.
// ============================================================================
#include "huffmancore.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <functional>
#include <queue>
#include <utility>

namespace huff {

namespace {

constexpr char          kMagic[4]  = {'H', 'U', 'F', 'F'};
constexpr std::uint8_t  kVersion   = 1;
constexpr std::size_t   kFixedHeaderSize = 4 + 1 + 1 + 1 + 4 + 4 + 8 + 4 + 2;   // = 29
// Максимальная длина кода. При 32-битных частотах глубина дерева Хаффмана
// не превышает ~47 (худший случай — числа Фибоначчи), так что 56 бит достаточно.
constexpr unsigned      kMaxCodeLength = 56;

// Коды форматов в заголовке.
constexpr std::uint8_t kFmtGray8 = 0;
constexpr std::uint8_t kFmtRgb32 = 1;
constexpr std::uint8_t kFmtArgb32 = 2;

// --- запись little-endian чисел в вектор байтов ---------------------------------
template <typename T>
void putLE(std::vector<std::uint8_t>& v, T value)
{
    const auto x = static_cast<std::uint64_t>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i)
        v.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xFF));
}

// --- чтение little-endian чисел с проверкой границ -------------------------------
class Cursor {
public:
    Cursor(const std::uint8_t* d, std::size_t n) : m_d(d), m_n(n) {}

    template <typename T>
    T get()
    {
        if (m_n - m_pos < sizeof(T))
            throw std::runtime_error("Файл .huff повреждён: заголовок обрезан");
        std::uint64_t v = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i)
            v |= static_cast<std::uint64_t>(m_d[m_pos + i]) << (8 * i);
        m_pos += sizeof(T);
        return static_cast<T>(v);
    }
    std::size_t pos() const { return m_pos; }

private:
    const std::uint8_t* m_d;
    std::size_t m_n;
    std::size_t m_pos = 0;
};

/// FNV-1a, 32 бита: простая контрольная сумма, написанная вручную.
std::uint32_t fnv1a(const std::vector<std::uint8_t>& data)
{
    std::uint32_t h = 2166136261u;
    for (std::uint8_t b : data) {
        h ^= b;
        h *= 16777619u;
    }
    return h;
}

int channelsFor(QImage::Format f)
{
    switch (f) {
    case QImage::Format_Grayscale8: return 1;
    case QImage::Format_RGB32:      return 3;
    case QImage::Format_ARGB32:     return 4;
    default:                        return 0;
    }
}

std::uint8_t codeFor(QImage::Format f)
{
    switch (f) {
    case QImage::Format_Grayscale8: return kFmtGray8;
    case QImage::Format_RGB32:      return kFmtRgb32;
    default:                        return kFmtArgb32;
    }
}

bool formatFromCode(std::uint8_t code, QImage::Format& out)
{
    switch (code) {
    case kFmtGray8:  out = QImage::Format_Grayscale8; return true;
    case kFmtRgb32:  out = QImage::Format_RGB32;      return true;
    case kFmtArgb32: out = QImage::Format_ARGB32;     return true;
    default:         return false;
    }
}

/// QImage (уже нормализованный) -> плоский поток байтов «строка за строкой».
std::vector<std::uint8_t> toRaw(const QImage& img)
{
    const int w  = img.width();
    const int h  = img.height();
    const int ch = channelsFor(img.format());
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(w) * h * ch);
    std::uint8_t* o = raw.data();

    for (int y = 0; y < h; ++y) {
        if (ch == 1) {
            std::memcpy(o, img.constScanLine(y), static_cast<std::size_t>(w));
            o += w;
        } else {
            const auto* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const QRgb p = line[x];
                *o++ = static_cast<std::uint8_t>(qRed(p));
                *o++ = static_cast<std::uint8_t>(qGreen(p));
                *o++ = static_cast<std::uint8_t>(qBlue(p));
                if (ch == 4)
                    *o++ = static_cast<std::uint8_t>(qAlpha(p));
            }
        }
    }
    return raw;
}

/// Плоский поток байтов -> QImage заданного формата.
QImage fromRaw(const std::vector<std::uint8_t>& raw, int w, int h, QImage::Format fmt)
{
    QImage img(w, h, fmt);
    if (img.isNull())
        throw std::runtime_error("Не удалось выделить память под изображение");

    const int ch = channelsFor(fmt);
    const std::uint8_t* p = raw.data();
    for (int y = 0; y < h; ++y) {
        if (ch == 1) {
            std::memcpy(img.scanLine(y), p, static_cast<std::size_t>(w));
            p += w;
        } else {
            auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const int r = *p++;
                const int g = *p++;
                const int b = *p++;
                const int a = (ch == 4) ? *p++ : 255;
                line[x] = qRgba(r, g, b, a);
            }
        }
    }
    return img;
}

} // namespace

// ----------------------------------------------------------------------------
//  Формат и сравнение изображений
// ----------------------------------------------------------------------------
QImage HuffmanCompressor::normalizeFormat(const QImage& img)
{
    if (img.isNull() || img.format() == QImage::Format_Grayscale8)
        return img;
    if (!img.hasAlphaChannel() && img.isGrayscale())
        return img.convertToFormat(QImage::Format_Grayscale8);
    if (img.hasAlphaChannel())
        return img.format() == QImage::Format_ARGB32 ? img
               : img.convertToFormat(QImage::Format_ARGB32);
    return img.format() == QImage::Format_RGB32 ? img
           : img.convertToFormat(QImage::Format_RGB32);
}

bool HuffmanCompressor::imagesEqual(const QImage& a, const QImage& b)
{
    if (a.isNull() || b.isNull())
        return a.isNull() && b.isNull();
    const QImage na = normalizeFormat(a);
    const QImage nb = normalizeFormat(b);
    if (na.size() != nb.size() || na.format() != nb.format())
        return false;
    return toRaw(na) == toRaw(nb);
}

// ----------------------------------------------------------------------------
//  Построение дерева и кодов
// ----------------------------------------------------------------------------
FreqTable HuffmanCompressor::countFrequencies(const std::vector<std::uint8_t>& data)
{
    FreqTable freq{};
    for (std::uint8_t b : data)
        ++freq[b];
    return freq;
}

HuffmanCompressor::Tree HuffmanCompressor::buildTree(const FreqTable& freq)
{
    Tree tree;
    tree.nodes.reserve(512);

    // Очередь с приоритетом (min-heap): пара {частота, индекс узла}.
    // Индекс в паре — детерминированный тай-брейк: при равных частотах первым
    // извлекается узел, созданный раньше. Поэтому кодер и декодер строят
    // одинаковое дерево по одной и той же таблице частот.
    using Item = std::pair<std::uint64_t, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;

    for (int s = 0; s < 256; ++s) {
        if (freq[s] == 0)
            continue;
        Node leaf;
        leaf.symbol = s;
        leaf.freq = freq[s];
        tree.nodes.push_back(leaf);
        pq.push({leaf.freq, static_cast<int>(tree.nodes.size()) - 1});
    }

    if (tree.nodes.empty())
        throw std::runtime_error("Нет данных для кодирования");

    // Вырожденный случай: в данных всего один различный байт.
    // Создаём корень с единственным левым потомком, чтобы код был длиной 1 бит.
    if (tree.nodes.size() == 1) {
        Node root;
        root.freq = tree.nodes[0].freq;
        root.left = 0;
        tree.nodes.push_back(root);
        tree.root = 1;
        return tree;
    }

    // Классический алгоритм: сливаем два наименее частых узла, пока не останется один.
    while (pq.size() > 1) {
        const auto a = pq.top(); pq.pop();
        const auto b = pq.top(); pq.pop();
        Node parent;
        parent.freq  = a.first + b.first;
        parent.left  = a.second;
        parent.right = b.second;
        tree.nodes.push_back(parent);
        pq.push({parent.freq, static_cast<int>(tree.nodes.size()) - 1});
    }
    tree.root = pq.top().second;
    return tree;
}

std::array<Code, 256> HuffmanCompressor::buildCodes(const Tree& tree)
{
    std::array<Code, 256> codes{};

    struct Frame { int node; std::uint64_t bits; unsigned len; };
    std::vector<Frame> stack;
    stack.push_back({tree.root, 0, 0});

    // Обход в глубину без рекурсии: левая ветка добавляет бит 0, правая — бит 1.
    while (!stack.empty()) {
        const Frame f = stack.back();
        stack.pop_back();
        const Node& n = tree.nodes[static_cast<std::size_t>(f.node)];

        if (n.isLeaf()) {
            codes[static_cast<std::size_t>(n.symbol)] =
                Code{f.bits, static_cast<std::uint8_t>(f.len)};
            continue;
        }
        if (f.len + 1 > kMaxCodeLength)
            throw std::runtime_error("Слишком длинный код Хаффмана");
        if (n.left >= 0)
            stack.push_back({n.left,  f.bits << 1,       f.len + 1});
        if (n.right >= 0)
            stack.push_back({n.right, (f.bits << 1) | 1, f.len + 1});
    }
    return codes;
}

void HuffmanCompressor::fillCodeStats(const FreqTable& freq,
                                      const std::array<Code, 256>& codes,
                                      std::uint64_t total,
                                      Stats& s)
{
    double H = 0.0, L = 0.0;
    for (int i = 0; i < 256; ++i) {
        if (freq[i] == 0)
            continue;
        const double p = static_cast<double>(freq[i]) / static_cast<double>(total);
        H -= p * std::log2(p);
        L += p * codes[static_cast<std::size_t>(i)].length;
    }
    s.entropy = H;
    s.avgCodeLength = L;
}

// ----------------------------------------------------------------------------
//  Сжатие
// ----------------------------------------------------------------------------
CompressResult HuffmanCompressor::compress(const QImage& input)
{
    if (input.isNull())
        throw std::runtime_error("Изображение не загружено");

    const QImage img = normalizeFormat(input);
    const std::vector<std::uint8_t> raw = toRaw(img);
    if (raw.size() > 0xFFFFFFFFull)
        throw std::runtime_error("Изображение слишком велико для формата .huff (32-битные частоты)");

    const FreqTable freq = countFrequencies(raw);
    const Tree tree = buildTree(freq);
    const auto codes = buildCodes(tree);

    // ---- заголовок --------------------------------------------------------------
    std::vector<std::uint8_t> out;
    out.reserve(kFixedHeaderSize + 5 * 256 + raw.size() / 2);

    out.insert(out.end(), kMagic, kMagic + 4);
    out.push_back(kVersion);
    out.push_back(codeFor(img.format()));
    out.push_back(static_cast<std::uint8_t>(channelsFor(img.format())));
    putLE<std::uint32_t>(out, static_cast<std::uint32_t>(img.width()));
    putLE<std::uint32_t>(out, static_cast<std::uint32_t>(img.height()));
    putLE<std::uint64_t>(out, raw.size());
    putLE<std::uint32_t>(out, fnv1a(raw));

    std::uint16_t used = 0;
    for (std::uint32_t f : freq)
        if (f > 0) ++used;
    putLE<std::uint16_t>(out, used);

    for (int s = 0; s < 256; ++s) {
        if (freq[s] == 0)
            continue;
        out.push_back(static_cast<std::uint8_t>(s));
        putLE<std::uint32_t>(out, freq[s]);
    }
    const std::size_t headerSize = out.size();

    // ---- битовый поток ------------------------------------------------------------
    BitWriter writer;
    writer.reserve(raw.size() / 2 + 16);
    for (std::uint8_t b : raw) {
        const Code& c = codes[b];
        writer.writeBits(c.bits, c.length);
    }
    writer.flush();
    out.insert(out.end(), writer.bytes().begin(), writer.bytes().end());

    // ---- результат ------------------------------------------------------------------
    CompressResult res;
    res.data = QByteArray(reinterpret_cast<const char*>(out.data()),
                          static_cast<qsizetype>(out.size()));
    Stats& s = res.stats;
    s.originalBytes   = raw.size();
    s.compressedBytes = out.size();
    s.headerBytes     = headerSize;
    s.payloadBytes    = writer.bytes().size();
    s.ratio           = static_cast<double>(s.originalBytes) / static_cast<double>(s.compressedBytes);
    s.savingPercent   = (1.0 - static_cast<double>(s.compressedBytes) / static_cast<double>(s.originalBytes)) * 100.0;
    fillCodeStats(freq, codes, raw.size(), s);
    return res;
}

// ----------------------------------------------------------------------------
//  Распаковка
// ----------------------------------------------------------------------------
DecompressResult HuffmanCompressor::decompress(const QByteArray& file)
{
    const auto* d = reinterpret_cast<const std::uint8_t*>(file.constData());
    const auto size = static_cast<std::size_t>(file.size());

    if (size < kFixedHeaderSize || std::memcmp(d, kMagic, 4) != 0)
        throw std::runtime_error("Неверная сигнатура: это не файл .huff");

    Cursor cur(d, size);
    for (int i = 0; i < 4; ++i)
        cur.get<std::uint8_t>();                         // пропускаем сигнатуру

    if (cur.get<std::uint8_t>() != kVersion)
        throw std::runtime_error("Неподдерживаемая версия формата .huff");

    QImage::Format fmt = QImage::Format_Invalid;
    if (!formatFromCode(cur.get<std::uint8_t>(), fmt))
        throw std::runtime_error("Неизвестный код формата изображения");
    const int channels = cur.get<std::uint8_t>();
    if (channels != channelsFor(fmt))
        throw std::runtime_error("Число каналов не соответствует формату");

    const std::uint32_t w = cur.get<std::uint32_t>();
    const std::uint32_t h = cur.get<std::uint32_t>();
    if (w == 0 || h == 0 || w > INT_MAX || h > INT_MAX)
        throw std::runtime_error("Недопустимые размеры изображения");

    const std::uint64_t symbolCount = cur.get<std::uint64_t>();
    const std::uint32_t checksum    = cur.get<std::uint32_t>();
    const unsigned used             = cur.get<std::uint16_t>();

    if (symbolCount != static_cast<std::uint64_t>(w) * h * static_cast<unsigned>(channels))
        throw std::runtime_error("Размер данных не соответствует W×H×каналы");
    if (used == 0 || used > 256)
        throw std::runtime_error("Недопустимый размер таблицы частот");

    // ---- таблица частот ------------------------------------------------------------
    FreqTable freq{};
    std::uint64_t sum = 0;
    int prev = -1;
    for (unsigned i = 0; i < used; ++i) {
        const int sym = cur.get<std::uint8_t>();
        const std::uint32_t f = cur.get<std::uint32_t>();
        if (sym <= prev || f == 0)
            throw std::runtime_error("Таблица частот повреждена");
        prev = sym;
        freq[static_cast<std::size_t>(sym)] = f;
        sum += f;
    }
    if (sum != symbolCount)
        throw std::runtime_error("Сумма частот не совпадает с размером данных");

    const std::size_t headerSize = cur.pos();
    const std::uint8_t* payload = d + headerSize;
    const std::size_t payloadSize = size - headerSize;

    // Каждый символ занимает >= 1 бита: защита от огромного выделения памяти
    // при испорченном заголовке.
    if (static_cast<std::uint64_t>(payloadSize) * 8 < symbolCount)
        throw std::runtime_error("Битовый поток обрезан");

    // ---- восстановление дерева и декодирование -----------------------------------------
    const Tree tree = buildTree(freq);
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(symbolCount));
    BitReader reader(payload, payloadSize);
    const Node* nodes = tree.nodes.data();

    for (std::size_t i = 0; i < raw.size(); ++i) {
        int n = tree.root;
        while (!nodes[n].isLeaf()) {
            n = reader.readBit() ? nodes[n].right : nodes[n].left;
            if (n < 0)
                throw std::runtime_error("Битовый поток повреждён: недопустимый код");
        }
        raw[i] = static_cast<std::uint8_t>(nodes[n].symbol);
    }

    // ---- результат --------------------------------------------------------------------------
    DecompressResult res;
    res.checksumOk = (fnv1a(raw) == checksum);
    res.image = fromRaw(raw, static_cast<int>(w), static_cast<int>(h), fmt);

    Stats& s = res.stats;
    s.originalBytes   = symbolCount;
    s.compressedBytes = size;
    s.headerBytes     = headerSize;
    s.payloadBytes    = payloadSize;
    s.ratio           = static_cast<double>(s.originalBytes) / static_cast<double>(s.compressedBytes);
    s.savingPercent   = (1.0 - static_cast<double>(s.compressedBytes) / static_cast<double>(s.originalBytes)) * 100.0;
    fillCodeStats(freq, buildCodes(tree), symbolCount, s);
    return res;
}

} // namespace huff
