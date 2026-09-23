#include "nnue.h"

#include "position.h"

#include <cstring>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if defined(__AVX2__) || defined(__SSE4_1__)
#include <immintrin.h>
#endif

#ifdef KURGAN_EMBEDDED_NET
extern "C"
{
    extern const uint8_t kEmbeddedNet[];
    extern const uint8_t kEmbeddedNetEnd[];
}
#endif

namespace
{
    std::string g_file;
    std::string g_error;
    bool g_enabled = true;

    // Bumped whenever the loaded network changes: cached accumulators are stale.
    uint32_t g_generation = 1;
}

#ifndef KURGAN_NNUE

bool nnue::loadEmbedded() { return false; }
void nnue::unload() {}
bool nnue::loaded() { return false; }
const std::string &nnue::error() { return g_error; }
void nnue::setEnabled(bool value) { g_enabled = value; }
bool nnue::enabled() { return g_enabled; }
bool nnue::active() { return false; }
const std::string &nnue::file() { return g_file; }
uint32_t nnue::hash() { return 0; }
int nnue::evaluate(const Position &) { return 0; }

void nnue::track(const Position &) {}
void nnue::update(const Position &, Piece, Square, bool) {}

#else

namespace
{
    constexpr uint32_t LEGACY_MAGIC = 0x314E4E4B; // "KNN1"

    struct Network
    {
        uint32_t hash = 0;
        std::vector<int16_t> ft;     // [INPUT_SIZE][HALF_DIM]
        std::vector<int16_t> ftBias; // [HALF_DIM]
        std::vector<int16_t> l2;     // [OUTPUT_BUCKETS][2][HALF_DIM]
        std::vector<int16_t> l2Bias; // [OUTPUT_BUCKETS]
    };

    Network *g_net = nullptr;
    uint32_t g_hash = 0;

    int bitCount(Bitboard b)
    {
#if defined(__GNUC__) || defined(__clang__)
        return __builtin_popcountll(b);
#elif defined(_MSC_VER)
        return static_cast<int>(__popcnt64(b));
#else
        int n = 0;
        for (; b; b &= b - 1)
            ++n;
        return n;
#endif
    }

    int bitScan(Bitboard b)
    {
#if defined(__GNUC__) || defined(__clang__)
        return __builtin_ctzll(b);
#elif defined(_MSC_VER)
        unsigned long index = 0;
        _BitScanForward64(&index, b);
        return static_cast<int>(index);
#else
        int n = 0;
        for (; (b & 1) == 0; b >>= 1)
            ++n;
        return n;
#endif
    }

    uint32_t fnv1a(const void *data, size_t size)
    {
        const uint8_t *bytes = static_cast<const uint8_t *>(data);
        uint32_t h = 2166136261u;
        for (size_t i = 0; i < size; ++i)
            h = (h ^ bytes[i]) * 16777619u;
        return h;
    }

    // A perspective sees the board from its own side, so the black view has the
    // ranks of every square flipped.
    Square perspectiveSquare(int colour, int square)
    {
        return static_cast<Square>(colour == BLACK ? square ^ 56 : square);
    }

    Square perspectiveKing(const Position &pos, int colour)
    {
        const Square king = pos.kingSquare(static_cast<Color>(colour));
        return king == SQ_NONE ? SQ_A1 : perspectiveSquare(colour, king);
    }

    // Vertical king buckets: the rank pair and the mirrored file pair.
    int kingBucket(Square sq)
    {
        const int file = sq & 7;
        return ((sq >> 3) & 6) | ((file > 3 ? 7 - file : file) >> 1);
    }

    int featureIndex(int colour, Square king, int pieceColour, int type, int square)
    {
        const Square oriented = perspectiveSquare(colour, square);
        const int mirrored = (king & 4) ? oriented ^ 7 : oriented;
        const int colourBase = pieceColour == colour ? 0 : nnue::COLOUR_STRIDE;
        return kingBucket(king) * nnue::FEATURES_PER_BUCKET + colourBase + type * nnue::PIECE_STRIDE + mirrored;
    }

    // Halves are never clamped
    void addRow(int16_t *acc, const Network &net, int index, bool add)
    {
        const int16_t *row = net.ft.data() + static_cast<size_t>(index) * nnue::HALF_DIM;
        int i = 0;
#if defined(__AVX2__)
        if (add)
        {
            for (; i + 16 <= nnue::HALF_DIM; i += 16)
            {
                const __m256i value = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(row + i));
                const __m256i accValue = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(acc + i));
                _mm256_storeu_si256(reinterpret_cast<__m256i *>(acc + i), _mm256_add_epi16(accValue, value));
            }
        }
        else
        {
            for (; i + 16 <= nnue::HALF_DIM; i += 16)
            {
                const __m256i value = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(row + i));
                const __m256i accValue = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(acc + i));
                _mm256_storeu_si256(reinterpret_cast<__m256i *>(acc + i), _mm256_sub_epi16(accValue, value));
            }
        }
#elif defined(__SSE4_1__)
        if (add)
        {
            for (; i + 8 <= nnue::HALF_DIM; i += 8)
            {
                const __m128i value = _mm_loadu_si128(reinterpret_cast<const __m128i *>(row + i));
                const __m128i accValue = _mm_loadu_si128(reinterpret_cast<const __m128i *>(acc + i));
                _mm_storeu_si128(reinterpret_cast<__m128i *>(acc + i), _mm_add_epi16(accValue, value));
            }
        }
        else
        {
            for (; i + 8 <= nnue::HALF_DIM; i += 8)
            {
                const __m128i value = _mm_loadu_si128(reinterpret_cast<const __m128i *>(row + i));
                const __m128i accValue = _mm_loadu_si128(reinterpret_cast<const __m128i *>(acc + i));
                _mm_storeu_si128(reinterpret_cast<__m128i *>(acc + i), _mm_sub_epi16(accValue, value));
            }
        }
#endif
        for (; i < nnue::HALF_DIM; ++i)
            acc[i] = static_cast<int16_t>(acc[i] + (add ? row[i] : -row[i]));
    }

    int32_t squareOf(int32_t value)
    {
        const int32_t clamped = value < 0 ? 0 : (value > nnue::QA ? nnue::QA : value);
        return clamped * clamped;
    }

#if defined(__AVX2__)
    int64_t headScore(const int16_t *acc0, const int16_t *acc1, const int16_t *row)
    {
        const __m256i zero = _mm256_setzero_si256();
        const __m256i high = _mm256_set1_epi32(nnue::QA);
        __m256i lo = _mm256_setzero_si256();
        __m256i hi = _mm256_setzero_si256();
        int i = 0;
        for (; i + 8 <= nnue::HALF_DIM; i += 8)
        {
            const __m256i value0 = _mm256_min_epi32(_mm256_max_epi32(
                _mm256_cvtepi16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i *>(acc0 + i))), zero), high);
            const __m256i weight0 = _mm256_cvtepi16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i *>(row + i)));
            const __m256i product0 = _mm256_mullo_epi32(_mm256_mullo_epi32(value0, value0), weight0);
            lo = _mm256_add_epi64(lo, _mm256_cvtepi32_epi64(_mm256_castsi256_si128(product0)));
            hi = _mm256_add_epi64(hi, _mm256_cvtepi32_epi64(_mm256_extracti128_si256(product0, 1)));

            const __m256i value1 = _mm256_min_epi32(_mm256_max_epi32(
                _mm256_cvtepi16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i *>(acc1 + i))), zero), high);
            const __m256i weight1 = _mm256_cvtepi16_epi32(
                _mm_loadu_si128(reinterpret_cast<const __m128i *>(row + nnue::HALF_DIM + i)));
            const __m256i product1 = _mm256_mullo_epi32(_mm256_mullo_epi32(value1, value1), weight1);
            lo = _mm256_add_epi64(lo, _mm256_cvtepi32_epi64(_mm256_castsi256_si128(product1)));
            hi = _mm256_add_epi64(hi, _mm256_cvtepi32_epi64(_mm256_extracti128_si256(product1, 1)));
        }

        lo = _mm256_add_epi64(lo, hi);
        alignas(32) int64_t lanes[4];
        _mm256_store_si256(reinterpret_cast<__m256i *>(lanes), lo);
        int64_t total = lanes[0] + lanes[1] + lanes[2] + lanes[3];
        for (; i < nnue::HALF_DIM; ++i)
            total += static_cast<int64_t>(squareOf(acc0[i])) * row[i]
                   + static_cast<int64_t>(squareOf(acc1[i])) * row[nnue::HALF_DIM + i];
        return total;
    }
#elif defined(__SSE4_1__)
    int64_t headScore(const int16_t *acc0, const int16_t *acc1, const int16_t *row)
    {
        const __m128i zero = _mm_setzero_si128();
        const __m128i high = _mm_set1_epi32(nnue::QA);
        __m128i lo = _mm_setzero_si128();
        __m128i hi = _mm_setzero_si128();
        int i = 0;
        for (; i + 4 <= nnue::HALF_DIM; i += 4)
        {
            const __m128i value0 = _mm_min_epi32(_mm_max_epi32(
                _mm_cvtepi16_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i *>(acc0 + i))), zero), high);
            const __m128i weight0 = _mm_cvtepi16_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i *>(row + i)));
            const __m128i product0 = _mm_mullo_epi32(_mm_mullo_epi32(value0, value0), weight0);
            lo = _mm_add_epi64(lo, _mm_cvtepi32_epi64(product0));
            hi = _mm_add_epi64(hi, _mm_cvtepi32_epi64(_mm_srli_si128(product0, 8)));

            const __m128i value1 = _mm_min_epi32(_mm_max_epi32(
                _mm_cvtepi16_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i *>(acc1 + i))), zero), high);
            const __m128i weight1 = _mm_cvtepi16_epi32(
                _mm_loadl_epi64(reinterpret_cast<const __m128i *>(row + nnue::HALF_DIM + i)));
            const __m128i product1 = _mm_mullo_epi32(_mm_mullo_epi32(value1, value1), weight1);
            lo = _mm_add_epi64(lo, _mm_cvtepi32_epi64(product1));
            hi = _mm_add_epi64(hi, _mm_cvtepi32_epi64(_mm_srli_si128(product1, 8)));
        }

        lo = _mm_add_epi64(lo, hi);
        alignas(16) int64_t lanes[2];
        _mm_store_si128(reinterpret_cast<__m128i *>(lanes), lo);
        int64_t total = lanes[0] + lanes[1];
        for (; i < nnue::HALF_DIM; ++i)
            total += static_cast<int64_t>(squareOf(acc0[i])) * row[i]
                   + static_cast<int64_t>(squareOf(acc1[i])) * row[nnue::HALF_DIM + i];
        return total;
    }
#else
    int64_t headScore(const int16_t *acc0, const int16_t *acc1, const int16_t *row)
    {
        int64_t total = 0;
        for (int i = 0; i < nnue::HALF_DIM; ++i)
            total += static_cast<int64_t>(squareOf(acc0[i])) * row[i]
                   + static_cast<int64_t>(squareOf(acc1[i])) * row[nnue::HALF_DIM + i];
        return total;
    }
#endif

    // One half per colour
    constexpr int TRACK_LIMIT = 4096; // piece changes before a rebuild

    struct Tracker
    {
        const Position *owner = nullptr;
        uint32_t generation = 0;
        int updates = 0;
        uint8_t dirty = 0; // rebuild the whole half
        bool valid = false;
        int16_t values[COLOR_NB][nnue::HALF_DIM];
    };

    thread_local Tracker g_tracker;

    void rebuildHalf(const Position &pos, const Network &net, int colour)
    {
        Tracker &t = g_tracker;
        const Square king = perspectiveKing(pos, colour);
        int16_t *acc = t.values[colour];

        for (int i = 0; i < nnue::HALF_DIM; ++i)
            acc[i] = net.ftBias[i];

        for (int pt = PAWN; pt <= KING; ++pt)
        {
            for (int pieceColour = WHITE; pieceColour <= BLACK; ++pieceColour)
            {
                Bitboard b = pos.byColor[pieceColour] & pos.byType[pt];
                while (b)
                {
                    const int sq = bitScan(b);
                    b &= b - 1;
                    addRow(acc, net, featureIndex(colour, king, pieceColour, pt, sq), true);
                }
            }
        }
    }

    // Accumulator matching 'pos', refreshed or rebuilt as far as it is out of date.
    void tracked(const Position &pos, const Network &net)
    {
        Tracker &t = g_tracker;
        if (t.owner != &pos || t.generation != g_generation || !t.valid)
        {
            t.owner = &pos;
            t.generation = g_generation;
            t.updates = 0;
            t.dirty = 0;
            t.valid = true;
            for (int c = 0; c < COLOR_NB; ++c)
                rebuildHalf(pos, net, c);
            return;
        }

        for (int c = 0; c < COLOR_NB; ++c)
        {
            const uint8_t bit = static_cast<uint8_t>(1 << c);
            if ((t.dirty & bit) == 0)
                continue;
            t.dirty &= static_cast<uint8_t>(~bit);
            rebuildHalf(pos, net, c);
        }
    }

    int outputBucket(const Position &pos)
    {
        const int pieces = bitCount(pos.byColor[WHITE] | pos.byColor[BLACK]);
        const int bucket = (pieces - 2) / (32 / nnue::OUTPUT_BUCKETS);
        return bucket < 0 ? 0 : (bucket < nnue::OUTPUT_BUCKETS ? bucket : nnue::OUTPUT_BUCKETS - 1);
    }

    // Nearest integer, ties away from zero.
    int64_t roundDiv(int64_t value, int64_t divisor)
    {
        const int64_t half = divisor / 2;
        return value >= 0 ? (value + half) / divisor : -((-value + half) / divisor);
    }
}

bool nnue::loadFromMemory(const uint8_t *data, size_t size, const std::string &name)
{
    unload();

    if (size >= 4 && std::memcmp(data, &LEGACY_MAGIC, 4) == 0)
    {
        g_error = "KNNUEv1 container, retrain";
        return false;
    }
    if (size != NET_SIZE)
    {
        g_error = "size ";
        g_error += std::to_string(size);
        g_error += ", a quantised net has ";
        g_error += std::to_string(NET_SIZE);
        return false;
    }

    Network net;
    net.ft.resize(static_cast<size_t>(INPUT_SIZE) * HALF_DIM);
    net.ftBias.resize(HALF_DIM);
    net.l2.resize(static_cast<size_t>(OUTPUT_BUCKETS) * 2 * HALF_DIM);
    net.l2Bias.resize(OUTPUT_BUCKETS);

    size_t offset = 0;
    const auto take = [&](void *dst, size_t bytes)
    {
        std::memcpy(dst, data + offset, bytes);
        offset += bytes;
    };
    take(net.ft.data(), net.ft.size() * sizeof(int16_t));
    take(net.ftBias.data(), net.ftBias.size() * sizeof(int16_t));
    take(net.l2.data(), net.l2.size() * sizeof(int16_t));
    take(net.l2Bias.data(), net.l2Bias.size() * sizeof(int16_t));

    net.hash = fnv1a(data, size);
    g_net = new Network(net);
    g_file = name;
    g_hash = net.hash;
    g_error.clear();
    return true;
}

bool nnue::loadEmbedded()
{
#ifdef KURGAN_EMBEDDED_NET
    return loadFromMemory(kEmbeddedNet, static_cast<size_t>(kEmbeddedNetEnd - kEmbeddedNet), "embedded");
#else
    return false;
#endif
}

void nnue::unload()
{
    delete g_net;
    g_net = nullptr;
    g_file.clear();
    g_hash = 0;
    ++g_generation;
}

bool nnue::loaded() { return g_net != nullptr; }
const std::string &nnue::error() { return g_error; }

void nnue::setEnabled(bool value)
{
    if (g_enabled == value)
        return;
    g_enabled = value;
    ++g_generation;
}

bool nnue::enabled() { return g_enabled; }

bool nnue::active() { return g_net != nullptr && g_enabled; }

const std::string &nnue::file() { return g_file; }

uint32_t nnue::hash() { return g_hash; }

void nnue::track(const Position &pos)
{
    Tracker &t = g_tracker;
    t.owner = &pos;
    t.generation = g_generation;
    t.updates = 0;
    t.dirty = 0;
    t.valid = active();
    if (!t.valid)
        return;

    for (int c = 0; c < COLOR_NB; ++c)
        rebuildHalf(pos, *g_net, c);
}

void nnue::update(const Position &pos, Piece piece, Square square, bool add)
{
    Tracker &t = g_tracker;
    if (piece == NO_PIECE || t.owner != &pos || !t.valid || !active())
        return;
    if (++t.updates > TRACK_LIMIT)
    {
        t.valid = false; // far from the last rebuild: start over on the next eval
        return;
    }

    const int pieceColour = colorOf(piece);
    const int type = typeOf(piece);
    if (type == KING)
        t.dirty |= static_cast<uint8_t>(1 << pieceColour); // it indexes its own perspective

    const Network &net = *g_net;
    for (int c = 0; c < COLOR_NB; ++c)
    {
        if (t.dirty & (1 << c))
            continue; // the pending rebuild covers this half
        addRow(t.values[c], net, featureIndex(c, perspectiveKing(pos, c), pieceColour, type, square), add);
    }
}

int nnue::evaluate(const Position &pos)
{
    const Network &net = *g_net;
    tracked(pos, net);

    const int bucket = outputBucket(pos);
    const int16_t *head = net.l2.data() + static_cast<size_t>(bucket) * 2 * HALF_DIM;
    const int64_t raw = headScore(g_tracker.values[pos.sideToMove],
                                  g_tracker.values[pos.sideToMove ^ 1], head)
        + static_cast<int64_t>(QA) * net.l2Bias[bucket];

    return static_cast<int>(roundDiv(static_cast<int64_t>(EVAL_SCALE) * raw, HEAD_DIVISOR));
}

#endif
