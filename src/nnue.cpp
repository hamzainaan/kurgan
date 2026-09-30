#include "nnue.h"

#include "position.h"
#include "stats.h"

#include <cstring>

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

#else

namespace
{
    constexpr uint32_t LEGACY_MAGIC = 0x314E4E4B; // "KNN1"

    struct Network
    {
        alignas(64) int16_t ft[static_cast<size_t>(nnue::INPUT_SIZE) * nnue::HALF_DIM];
        alignas(64) int16_t ftBias[nnue::HALF_DIM];
        alignas(64) int16_t l2[nnue::OUTPUT_BUCKETS * 2 * nnue::HALF_DIM];
        alignas(64) int16_t l2Bias[nnue::OUTPUT_BUCKETS];
        uint32_t hash = 0;
        bool narrowHead = false;
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

    // Thirty-two king buckets: eight ranks by four horizontally mirrored file
    // groups, the bucketing Stockfish's HalfKA_hm uses.
    int kingBucket(Square sq)
    {
        const int file = sq & 7;
        return ((sq >> 3) << 2) | (file > 3 ? 7 - file : file);
    }

#if defined(__AVX512BW__)
    using Vec = __m512i;
    inline Vec vload(const int16_t *p) { return _mm512_load_si512(p); }
    inline void vstore(int16_t *p, Vec v) { _mm512_store_si512(p, v); }
    inline Vec vadd16(Vec a, Vec b) { return _mm512_add_epi16(a, b); }
    inline Vec vsub16(Vec a, Vec b) { return _mm512_sub_epi16(a, b); }
    inline Vec vclamp16(Vec v)
    {
        return _mm512_min_epi16(_mm512_max_epi16(v, _mm512_setzero_si512()), _mm512_set1_epi16(nnue::QA));
    }
    inline Vec vmul16(Vec a, Vec b) { return _mm512_mullo_epi16(a, b); }
    inline Vec vmadd16(Vec a, Vec b) { return _mm512_madd_epi16(a, b); }
    inline Vec vadd32(Vec a, Vec b) { return _mm512_add_epi32(a, b); }
    inline Vec vzero() { return _mm512_setzero_si512(); }
    inline int64_t vsum32(Vec v)
    {
        return _mm512_reduce_add_epi64(_mm512_add_epi64(_mm512_cvtepi32_epi64(_mm512_castsi512_si256(v)),
                                                        _mm512_cvtepi32_epi64(_mm512_extracti64x4_epi64(v, 1))));
    }
#elif defined(__AVX2__)
    using Vec = __m256i;
    inline Vec vload(const int16_t *p) { return _mm256_load_si256(reinterpret_cast<const Vec *>(p)); }
    inline void vstore(int16_t *p, Vec v) { _mm256_store_si256(reinterpret_cast<Vec *>(p), v); }
    inline Vec vadd16(Vec a, Vec b) { return _mm256_add_epi16(a, b); }
    inline Vec vsub16(Vec a, Vec b) { return _mm256_sub_epi16(a, b); }
    inline Vec vclamp16(Vec v)
    {
        return _mm256_min_epi16(_mm256_max_epi16(v, _mm256_setzero_si256()), _mm256_set1_epi16(nnue::QA));
    }
    inline Vec vmul16(Vec a, Vec b) { return _mm256_mullo_epi16(a, b); }
    inline Vec vmadd16(Vec a, Vec b) { return _mm256_madd_epi16(a, b); }
    inline Vec vadd32(Vec a, Vec b) { return _mm256_add_epi32(a, b); }
    inline Vec vzero() { return _mm256_setzero_si256(); }
    inline int64_t vsum32(Vec v)
    {
        const __m256i wide = _mm256_add_epi64(_mm256_cvtepi32_epi64(_mm256_castsi256_si128(v)),
                                              _mm256_cvtepi32_epi64(_mm256_extracti128_si256(v, 1)));
        const __m128i sum = _mm_add_epi64(_mm256_castsi256_si128(wide), _mm256_extracti128_si256(wide, 1));
        return _mm_cvtsi128_si64(sum) + _mm_extract_epi64(sum, 1);
    }
#elif defined(__SSE4_1__)
    using Vec = __m128i;
    inline Vec vload(const int16_t *p) { return _mm_load_si128(reinterpret_cast<const Vec *>(p)); }
    inline void vstore(int16_t *p, Vec v) { _mm_store_si128(reinterpret_cast<Vec *>(p), v); }
    inline Vec vadd16(Vec a, Vec b) { return _mm_add_epi16(a, b); }
    inline Vec vsub16(Vec a, Vec b) { return _mm_sub_epi16(a, b); }
    inline Vec vclamp16(Vec v)
    {
        return _mm_min_epi16(_mm_max_epi16(v, _mm_setzero_si128()), _mm_set1_epi16(nnue::QA));
    }
    inline Vec vmul16(Vec a, Vec b) { return _mm_mullo_epi16(a, b); }
    inline Vec vmadd16(Vec a, Vec b) { return _mm_madd_epi16(a, b); }
    inline Vec vadd32(Vec a, Vec b) { return _mm_add_epi32(a, b); }
    inline Vec vzero() { return _mm_setzero_si128(); }
    inline int64_t vsum32(Vec v)
    {
        const __m128i wide = _mm_add_epi64(_mm_cvtepi32_epi64(v), _mm_cvtepi32_epi64(_mm_srli_si128(v, 8)));
        return _mm_cvtsi128_si64(wide) + _mm_extract_epi64(wide, 1);
    }
#endif

#if defined(__AVX2__) || defined(__SSE4_1__)
    constexpr int VEC_LANES = static_cast<int>(sizeof(Vec) / sizeof(int16_t));
    constexpr int TILE_REGS = 8;
    constexpr int TILE = TILE_REGS * VEC_LANES;
    static_assert(nnue::HALF_DIM % TILE == 0);
#endif

    // Halves are never clamped
    void applyRows(int16_t *acc, const Network &net, const int *adds, int addCount, const int *subs, int subCount)
    {
#if defined(__AVX2__) || defined(__SSE4_1__)
        for (int base = 0; base < nnue::HALF_DIM; base += TILE)
        {
            Vec regs[TILE_REGS];
            for (int k = 0; k < TILE_REGS; ++k)
                regs[k] = vload(acc + base + k * VEC_LANES);
            for (int n = 0; n < addCount; ++n)
            {
                const int16_t *row = net.ft + static_cast<size_t>(adds[n]) * nnue::HALF_DIM + base;
                for (int k = 0; k < TILE_REGS; ++k)
                    regs[k] = vadd16(regs[k], vload(row + k * VEC_LANES));
            }
            for (int n = 0; n < subCount; ++n)
            {
                const int16_t *row = net.ft + static_cast<size_t>(subs[n]) * nnue::HALF_DIM + base;
                for (int k = 0; k < TILE_REGS; ++k)
                    regs[k] = vsub16(regs[k], vload(row + k * VEC_LANES));
            }
            for (int k = 0; k < TILE_REGS; ++k)
                vstore(acc + base + k * VEC_LANES, regs[k]);
        }
#else
        for (int n = 0; n < addCount; ++n)
        {
            const int16_t *row = net.ft + static_cast<size_t>(adds[n]) * nnue::HALF_DIM;
            for (int i = 0; i < nnue::HALF_DIM; ++i)
                acc[i] = static_cast<int16_t>(acc[i] + row[i]);
        }
        for (int n = 0; n < subCount; ++n)
        {
            const int16_t *row = net.ft + static_cast<size_t>(subs[n]) * nnue::HALF_DIM;
            for (int i = 0; i < nnue::HALF_DIM; ++i)
                acc[i] = static_cast<int16_t>(acc[i] - row[i]);
        }
#endif
    }

    int32_t squareOf(int32_t value)
    {
        const int32_t clamped = value < 0 ? 0 : (value > nnue::QA ? nnue::QA : value);
        return clamped * clamped;
    }

#if defined(__AVX2__)
    int64_t headWide(const int16_t *acc0, const int16_t *acc1, const int16_t *row)
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
    int64_t headWide(const int16_t *acc0, const int16_t *acc1, const int16_t *row)
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
    int64_t headWide(const int16_t *acc0, const int16_t *acc1, const int16_t *row)
    {
        int64_t total = 0;
        for (int i = 0; i < nnue::HALF_DIM; ++i)
            total += static_cast<int64_t>(squareOf(acc0[i])) * row[i]
                   + static_cast<int64_t>(squareOf(acc1[i])) * row[nnue::HALF_DIM + i];
        return total;
    }
#endif

#if defined(__AVX2__) || defined(__SSE4_1__)
    int64_t headNarrow(const int16_t *acc0, const int16_t *acc1, const int16_t *row)
    {
        Vec sum = vzero();
        for (int i = 0; i < nnue::HALF_DIM; i += VEC_LANES)
        {
            const Vec value0 = vclamp16(vload(acc0 + i));
            const Vec value1 = vclamp16(vload(acc1 + i));
            sum = vadd32(sum, vmadd16(vmul16(value0, vload(row + i)), value0));
            sum = vadd32(sum, vmadd16(vmul16(value1, vload(row + nnue::HALF_DIM + i)), value1));
        }
        return vsum32(sum);
    }
#endif

    int64_t headScore([[maybe_unused]] const Network &net, const int16_t *acc0, const int16_t *acc1, const int16_t *row)
    {
#if defined(__AVX2__) || defined(__SSE4_1__)
        if (net.narrowHead)
            return headNarrow(acc0, acc1, row);
#endif
        return headWide(acc0, acc1, row);
    }

    struct HalfCache
    {
        alignas(64) int16_t values[nnue::HALF_DIM];
        Bitboard byPiece[PIECE_NB] = {};
        uint32_t generation = 0;
    };

    thread_local HalfCache g_halfCache[COLOR_NB][64];
    thread_local Square g_lastKing[COLOR_NB] = {SQ_NONE, SQ_NONE};

    const int16_t *refreshHalf(const Position &pos, const Network &net, int colour)
    {
        const Square king = perspectiveKing(pos, colour);
        HalfCache &cache = g_halfCache[colour][king];
        stats::Tally &tally = stats::current();

        if (cache.generation != g_generation)
        {
            ++tally.evalRebuilds;
            std::memcpy(cache.values, net.ftBias, sizeof(cache.values));
            std::memset(cache.byPiece, 0, sizeof(cache.byPiece));
            cache.generation = g_generation;
        }
        else if (g_lastKing[colour] != king)
            ++tally.evalRefills;
        g_lastKing[colour] = king;

        const int base = kingBucket(king) * nnue::FEATURES_PER_BUCKET;
        const int flip = (colour == BLACK ? 56 : 0) ^ ((king & 4) ? 7 : 0);
        int adds[64];
        int subs[64];
        int addCount = 0;
        int subCount = 0;

        for (int pieceColour = WHITE; pieceColour <= BLACK; ++pieceColour)
        {
            const int colourBase = base + (pieceColour == colour ? 0 : nnue::COLOUR_STRIDE);
            for (int pt = PAWN; pt <= KING; ++pt)
            {
                const Piece pc = makePiece(static_cast<Color>(pieceColour), static_cast<PieceType>(pt));
                const Bitboard now = pos.byPiece[pc];
                const Bitboard changed = cache.byPiece[pc] ^ now;
                if (!changed)
                    continue;
                cache.byPiece[pc] = now;

                const int offset = colourBase + pt * nnue::PIECE_STRIDE;
                for (Bitboard b = changed & now; b; b &= b - 1)
                    adds[addCount++] = offset + (bitScan(b) ^ flip);
                for (Bitboard b = changed & ~now; b; b &= b - 1)
                    subs[subCount++] = offset + (bitScan(b) ^ flip);
            }
        }

        if (addCount + subCount)
        {
            applyRows(cache.values, net, adds, addCount, subs, subCount);
            tally.evalUpdates += static_cast<uint64_t>(addCount + subCount);
        }
        return cache.values;
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

    Network *net = new Network;
    size_t at = 0;
    const auto take = [&](int16_t *dst, size_t count)
    {
        std::memcpy(dst, data + at, count * sizeof(int16_t));
        at += count * sizeof(int16_t);
    };
    take(net->ft, static_cast<size_t>(INPUT_SIZE) * HALF_DIM);
    take(net->ftBias, HALF_DIM);
    take(net->l2, static_cast<size_t>(OUTPUT_BUCKETS) * 2 * HALF_DIM);
    take(net->l2Bias, OUTPUT_BUCKETS);

    net->narrowHead = true;
    for (const int16_t w : net->l2)
        if (w < -128 || w > 128)
            net->narrowHead = false;

    net->hash = fnv1a(data, size);
    g_net = net;
    g_file = name;
    g_hash = net->hash;
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

int nnue::evaluate(const Position &pos)
{
    const Network &net = *g_net;
    const int16_t *us = refreshHalf(pos, net, pos.sideToMove);
    const int16_t *them = refreshHalf(pos, net, pos.sideToMove ^ 1);

    const int bucket = outputBucket(pos);
    const int16_t *head = net.l2 + static_cast<size_t>(bucket) * 2 * HALF_DIM;
    const int64_t raw = headScore(net, us, them, head) + static_cast<int64_t>(QA) * net.l2Bias[bucket];

    return static_cast<int>(roundDiv(static_cast<int64_t>(EVAL_SCALE) * raw, HEAD_DIVISOR));
}

#endif
